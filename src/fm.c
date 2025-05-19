#include "fm.h"

#include "async.h"
#include "config.h"
#include "dir.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros_defs.h"
#include "notify.h"
#include "path.h"
#include "pathlist.h"
#include "util.h"

#include <errno.h>
#include <ev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgen.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define i_declared
#define i_TYPE vec_dir, Dir *
#include "stc/vec.h"

static inline void on_cursor_moved(Fm *fm, bool delay_action);
static void on_cursor_resting(EV_P_ ev_timer *w, int revents);
static void fm_update_watchers(Fm *fm);
static void fm_remove_preview(Fm *fm);
static void fm_populate(Fm *fm);

void fm_init(Fm *fm) {
  fm->paste.mode = PASTE_MODE_COPY;

  ev_timer_init(&fm->cursor_resting_timer, on_cursor_resting, 0,
                cfg.preview_delay / 1000.0);
  fm->cursor_resting_timer.data = to_lfm(fm);

  if (cfg.startpath) {
    if (chdir(cfg.startpath) != 0) {
      lfm_error(to_lfm(fm), "chdir: %s", strerror(errno));
    } else {
      fm->pwd = strdup(cfg.startpath);
    }
  } else {
    const char *s = getenv("PWD");
    if (s) {
      fm->pwd = strdup(s);
    } else {
      char pwd[PATH_MAX];
      if (getcwd(pwd, sizeof pwd) == NULL) {
        perror("getcwd");
        _exit(1);
      }
      fm->pwd = strdup(pwd);
    }
  }

  fm->dirs.length = vec_int_size(&cfg.ratios) - (cfg.preview ? 1 : 0);
  vec_dir_resize(&fm->dirs.visible, fm->dirs.length, 0);

  pathlist_init(&fm->selection.current);
  pathlist_init(&fm->selection.keep_in_visual);
  pathlist_init(&fm->selection.previous);
  pathlist_init(&fm->paste.buffer);

  fm_populate(fm);
  if (cfg.startfile) {
    fm_move_cursor_to(fm, cfg.startfile);
  }

  fm_update_watchers(fm);
  on_cursor_moved(fm, false);
}

void fm_deinit(Fm *fm) {
  vec_dir_drop(&fm->dirs.visible);
  pathlist_deinit(&fm->selection.current);
  pathlist_deinit(&fm->selection.keep_in_visual);
  pathlist_deinit(&fm->selection.previous);
  pathlist_deinit(&fm->paste.buffer);
  xfree(fm->automark);
  xfree(fm->pwd);
}

static void fm_populate(Fm *fm) {
  fm->dirs.visible.data[0] = loader_dir_from_path(&to_lfm(fm)->loader, fm->pwd,
                                                  true); /* current dir */
  fm->dirs.visible.data[0]->visible = true;
  Dir *dir = fm_current_dir(fm);
  for (uint32_t i = 1; i < fm->dirs.length; i++) {
    const char *parent = path_parent_s(dir_path(dir));
    if (parent) {
      dir = loader_dir_from_path(&to_lfm(fm)->loader, parent, true);
      dir->visible = true;
      fm->dirs.visible.data[i] = dir;
      if (dir_loading(dir)) {
        dir_cursor_move_to(dir, dir_name(fm->dirs.visible.data[i - 1]),
                           fm->height, cfg.scrolloff);
      }
    } else {
      fm->dirs.visible.data[i] = NULL;
    }
  }
}

void fm_recol(Fm *fm) {
  fm_remove_preview(fm);
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible.data[i]) {
      fm->dirs.visible.data[i]->visible = false;
    }
  }

  const uint32_t l = max(1, vec_int_size(&cfg.ratios) - (cfg.preview ? 1 : 0));
  vec_dir_resize(&fm->dirs.visible, l, 0);
  fm->dirs.length = l;

  fm_populate(fm);
  fm_update_watchers(fm);
  on_cursor_moved(fm, false);
}

static inline bool fm_chdir_impl(Fm *fm, const char *path, bool save, bool hook,
                                 bool async) {
  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  if (async) {
    async_chdir(&to_lfm(fm)->async, path, hook);
  } else {
    if (chdir(path) == 0) {
      setenv("PWD", path, true);
    } else {
      lfm_error(to_lfm(fm), "chdir: %s", strerror(errno));
      return false;
    }
  }

  notify_remove_watchers(&to_lfm(fm)->notify);

  xfree(fm->pwd);
  fm->pwd = strdup(path);

  if (save) {
    xfree(fm->automark);
    fm->automark =
        fm_current_dir(fm)->error ? NULL : strdup(dir_path(fm_current_dir(fm)));
  }

  fm_remove_preview(fm);
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible.data[i]) {
      fm->dirs.visible.data[i]->visible = false;
    }
  }

  fm_populate(fm);
  fm_update_watchers(fm);
  on_cursor_moved(fm, false);

  if (!async && hook) {
    lfm_run_hook(to_lfm(fm), LFM_HOOK_CHDIRPOST);
  }

  return true;
}

bool fm_sync_chdir(Fm *fm, const char *path, bool save, bool hook) {
  return fm_chdir_impl(fm, path, save, hook, false);
}

bool fm_async_chdir(Fm *fm, const char *path, bool save, bool hook) {
  return fm_chdir_impl(fm, path, save, hook, true);
}

static inline void fm_update_watchers(Fm *fm) {
  // watcher for preview is updated in update_preview
  notify_remove_watchers(&to_lfm(fm)->notify);
  for (size_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible.data[i]) {
      async_notify_add(&to_lfm(fm)->async, fm->dirs.visible.data[i]);
    }
  }
}

/* TODO: maybe we can select the closest non-hidden file in case the
 * current one will be hidden (on 2021-10-17) */
static inline void fm_sort_and_reselect(Fm *fm, Dir *dir) {
  if (!dir) {
    return;
  }

  /* TODO: shouldn't apply the global hidden setting (on 2022-10-09) */
  dir->settings.hidden = cfg.dir_settings.hidden;
  const File *file = dir_current_file(dir);
  dir_sort(dir);
  if (file) {
    dir_cursor_move_to(dir, file_name(file), fm->height, cfg.scrolloff);
  }
}

void fm_sort(Fm *fm) {
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    fm_sort_and_reselect(fm, fm->dirs.visible.data[i]);
  }
  fm_sort_and_reselect(fm, fm->dirs.preview);
}

void fm_hidden_set(Fm *fm, bool hidden) {
  cfg.dir_settings.hidden = hidden;
  fm_sort(fm);
  on_cursor_moved(fm, false);
}

void fm_check_dirs(const Fm *fm) {
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible.data[i] && !dir_check(fm->dirs.visible.data[i])) {
      loader_dir_reload(&to_lfm(fm)->loader, fm->dirs.visible.data[i]);
    }
  }

  if (fm->dirs.preview && !dir_check(fm->dirs.preview)) {
    loader_dir_reload(&to_lfm(fm)->loader, fm->dirs.preview);
  }
}

void fm_drop_cache(Fm *fm) {
  log_trace("fm_drop_cache");

  notify_remove_watchers(&to_lfm(fm)->notify);
  fm_remove_preview(fm);

  loader_drop_dir_cache(&to_lfm(fm)->loader);

  fm_populate(fm);
  fm_update_watchers(fm);
  on_cursor_moved(fm, false);
}

void fm_reload(Fm *fm) {
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible.data[i]) {
      async_dir_load(&to_lfm(fm)->async, fm->dirs.visible.data[i], true);
    }
  }
  if (fm->dirs.preview) {
    async_dir_load(&to_lfm(fm)->async, fm->dirs.preview, true);
  }
}

static inline void fm_remove_preview(Fm *fm) {
  if (fm->dirs.preview) {
    log_trace("removing preview %s", dir_path(fm->dirs.preview));
    notify_remove_watcher(&to_lfm(fm)->notify, fm->dirs.preview);
    fm->dirs.preview->visible = false;
    fm->dirs.preview = NULL;
  }
}

// ev calls this with revents == 256
// we invoke it manually with revents == 0 to indicate that the actual loading
// of the directory has already been arranged
static void on_cursor_resting(EV_P_ ev_timer *w, int revents) {
  log_trace("on_cursor_resting revents=%d", revents);
  if (revents != 0) {
    ev_timer_stop(loop, w);
  }

  Lfm *lfm = w->data;

  Dir *dir = lfm->fm.dirs.preview;
  if (dir) {
    if (revents != 0) {
      if (dir->status == DIR_LOADING_DELAYED) {
        async_dir_load(&lfm->async, lfm->fm.dirs.preview, false);
      } else {
        async_dir_check(&lfm->async, dir);
      }
    }
    async_notify_preview_add(&lfm->async, lfm->fm.dirs.preview);
  }
}

void fm_update_preview(Fm *fm) {
  on_cursor_moved(fm, false);
}

static inline void on_cursor_moved(Fm *fm, bool delay_action) {
  delay_action &= cfg.preview_delay > 0;
  log_trace("on_cursor_moved delay_action=%d", delay_action);

  if (!cfg.preview) {
    fm_remove_preview(fm);
    return;
  }

  const File *file = fm_current_file(fm);
  bool is_directory_preview = file != NULL && file_isdir(file);
  bool is_same_preview = file != NULL && fm->dirs.preview != NULL &&
                         streq(dir_path(fm->dirs.preview), file_path(file));

  if (!is_same_preview) {
    fm_remove_preview(fm);
  }

  if (is_directory_preview && !is_same_preview) {
    fm->dirs.preview = loader_dir_from_path(&to_lfm(fm)->loader,
                                            file_path(file), !delay_action);
    fm->dirs.preview->visible = true;
  }

  if (!is_same_preview) {
    // invoke on_cursor_resting (on delay) to set up watcher/actually load the
    // directory

    if (delay_action) {
      ev_timer_again(to_lfm(fm)->loop, &fm->cursor_resting_timer);
    } else {
      ev_invoke(to_lfm(fm)->loop, &fm->cursor_resting_timer, 0);
    }
  }
}

static inline void fm_selection_toggle(Fm *fm, const char *path,
                                       bool run_hook) {
  if (!pathlist_remove(&fm->selection.current, path)) {
    fm_selection_add(fm, path, false);
  }
  if (run_hook) {
    lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
  }
}

void fm_selection_toggle_current(Fm *fm) {
  if (fm->visual.active) {
    return;
  }
  File *file = fm_current_file(fm);
  if (file) {
    fm_selection_toggle(fm, file_path(file), true);
  }
}

void fm_selection_add(Fm *fm, const char *path, bool run_hook) {
  pathlist_add(&fm->selection.current, path);
  if (run_hook) {
    lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
  }
}

void fm_selection_clear(Fm *fm) {
  log_trace("fm_selection_clear");
  if (pathlist_size(&fm->selection.current) > 0) {
    pathlist tmp = fm->selection.previous;
    fm->selection.previous = fm->selection.current;
    fm->selection.current = tmp;
    pathlist_clear(&fm->selection.current);
    lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
  }
}

void fm_selection_reverse(Fm *fm) {
  const Dir *dir = fm_current_dir(fm);
  for (uint32_t i = 0; i < dir->length; i++) {
    fm_selection_toggle(fm, file_path(dir->files[i]), false);
  }
  lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
}

void fm_on_visual_enter(Fm *fm) {
  if (fm->visual.active) {
    return;
  }

  Dir *dir = fm_current_dir(fm);
  if (dir->length == 0) {
    return;
  }

  fm->visual.active = true;
  fm->visual.anchor = dir->ind;
  fm_selection_add(fm, file_path(dir->files[dir->ind]), false);
  pathlist_clear(&fm->selection.keep_in_visual);
  c_foreach(it, pathlist, fm->selection.current) {
    pathlist_add(&fm->selection.keep_in_visual, *it.ref);
  }
  lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
}

void fm_on_visual_exit(Fm *fm) {
  if (!fm->visual.active) {
    return;
  }

  fm->visual.active = false;
  fm->visual.anchor = 0;
  pathlist_clear(&fm->selection.keep_in_visual);
}

static void selection_visual_update(Fm *fm, uint32_t origin, uint32_t from,
                                    uint32_t to) {
  uint32_t hi, lo;
  if (from >= origin) {
    if (to > from) {
      lo = from + 1;
      hi = to;
    } else if (to < origin) {
      hi = from;
      lo = to;
    } else {
      hi = from;
      lo = to + 1;
    }
  } else {
    if (to < from) {
      lo = to;
      hi = from - 1;
    } else if (to > origin) {
      lo = from;
      hi = to;
    } else {
      lo = from;
      hi = to - 1;
    }
  }
  const Dir *dir = fm_current_dir(fm);
  for (; lo <= hi; lo++) {
    // never unselect the old selection
    if (!pathlist_contains(&fm->selection.keep_in_visual,
                           file_path(dir->files[lo]))) {
      fm_selection_toggle(fm, file_path(dir->files[lo]), false);
    }
  }
  lfm_run_hook(to_lfm(fm), LFM_HOOK_SELECTION);
}

void fm_selection_write(const Fm *fm, const char *path) {
  char *dir, *buf = strdup(path);
  dir = dirname(buf);
  mkdir_p(dir, 755);
  xfree(buf);

  FILE *fp = fopen(path, "w");
  if (!fp) {
    lfm_error(to_lfm(fm), "selfile: %s", strerror(errno));
    return;
  }

  if (pathlist_size(&fm->selection.current) > 0) {
    for (pathlist_iter it = pathlist_begin(&fm->selection.current); it.ref;
         pathlist_next(&it)) {
      fputs(*it.ref, fp);
      fputc('\n', fp);
    }
  } else {
    const File *file = fm_current_file(fm);
    if (file) {
      fputs(file_path(file), fp);
      fputc('\n', fp);
    }
  }
  fclose(fp);
}

void fm_paste_mode_set(Fm *fm, paste_mode mode) {
  fm->paste.mode = mode;
  if (pathlist_size(&fm->selection.current) == 0) {
    fm_selection_toggle_current(fm);
  }
  pathlist_deinit(&fm->paste.buffer);
  fm->paste.buffer = fm->selection.current;
  pathlist_init(&fm->selection.current);
}

bool fm_cursor_move(Fm *fm, int32_t ct) {
  Dir *dir = fm_current_dir(fm);
  uint32_t cur = dir->ind;
  dir_cursor_move(dir, ct, fm->height, cfg.scrolloff);
  if (dir->ind != cur) {
    if (fm->visual.active) {
      selection_visual_update(fm, fm->visual.anchor, cur, dir->ind);
    }
    on_cursor_moved(fm, true);
  }
  return dir->ind != cur;
}

void fm_move_cursor_to(Fm *fm, const char *name) {
  dir_cursor_move_to(fm_current_dir(fm), name, fm->height, cfg.scrolloff);
  on_cursor_moved(fm, false);
}

bool fm_scroll_up(Fm *fm) {
  Dir *dir = fm_current_dir(fm);
  if (dir->ind > 0 && dir->ind == dir->pos) {
    return fm_up(fm, 1);
  }
  if (dir->pos < fm->height - cfg.scrolloff - 1) {
    dir->pos++;
  } else {
    dir->pos = fm->height - cfg.scrolloff - 1;
    dir->ind--;
    if (dir->ind > dir->length - cfg.scrolloff - 1) {
      dir->ind = dir->length - cfg.scrolloff - 1;
    }
    on_cursor_moved(fm, false);
  }
  return true;
}

bool fm_scroll_down(Fm *fm) {
  Dir *dir = fm_current_dir(fm);
  if (dir->length - dir->ind + dir->pos - 1 < fm->height) {
    return fm_down(fm, 1);
  }
  if (dir->pos > cfg.scrolloff) {
    dir->pos--;
  } else {
    dir->pos = cfg.scrolloff;
    dir->ind++;
    if (dir->ind < dir->pos) {
      dir->ind = dir->pos;
    }
    on_cursor_moved(fm, false);
  }
  return true;
}

File *fm_open(Fm *fm) {
  File *file = fm_current_file(fm);
  if (!file) {
    return NULL;
  }

  if (!file_isdir(file)) {
    return file;
  }

  fm_async_chdir(fm, file_path(file), false, false);
  return NULL;
}

/* TODO: allow updir into directories that don't exist so we can move out of
 * deleted directories (on 2021-11-18) */
bool fm_updir(Fm *fm) {
  if (dir_isroot(fm_current_dir(fm))) {
    return false;
  }

  fm_async_chdir(fm, path_parent_s(dir_path(fm_current_dir(fm))), false, false);
  on_cursor_moved(fm, false);
  return true;
}

void fm_filter(Fm *fm, Filter *filter) {
  Dir *dir = fm_current_dir(fm);
  File *file = dir_current_file(dir);
  dir_filter(dir, filter);
  dir_cursor_move_to(dir, file ? file_name(file) : NULL, fm->height,
                     cfg.scrolloff);
  on_cursor_moved(fm, false);
}

/* TODO: To reload flattened directories, more notify watchers are needed (on
 * 2022-02-06) */
void fm_flatten(Fm *fm, uint32_t level) {
  fm_current_dir(fm)->flatten_level = level;
  async_dir_load(&to_lfm(fm)->async, fm_current_dir(fm), false);
}

void fm_on_resize(Fm *fm, uint32_t height) {
  uint32_t scrolloff = cfg.scrolloff;
  if (height < cfg.scrolloff * 2) {
    scrolloff = height / 2;
  }

  // TODO: is there a way to restore the position when just undoing a previous
  // resize?
  c_foreach(v, dircache, to_lfm(fm)->loader.dc) {
    Dir *dir = v.ref->second;
    if (height > fm->height) {
      // terminal grew
      uint32_t scrolloff_top = dir->ind;
      if (scrolloff_top > scrolloff) {
        scrolloff_top = scrolloff;
      }
      if (dir->length + dir->pos < height + dir->ind) {
        dir->pos = height - dir->length + dir->ind;
      }
      if (dir->length > height && dir->pos < scrolloff_top) {
        dir->pos = scrolloff_top;
      }
    } else if (height < fm->height) {
      // terminal shrinked
      uint32_t scrolloff = cfg.scrolloff;
      if (height < scrolloff * 2) {
        scrolloff = height / 2;
      }
      if (scrolloff >= dir->length - dir->ind) {
        // closer to the end of directory than scrolloff
        dir->pos = height - (dir->length - dir->ind);
      } else if (dir->pos + scrolloff >= height) {
        dir->pos = height - scrolloff - 1;
      }
    }
  }

  fm->height = height;
}
