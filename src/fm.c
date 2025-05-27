#include "fm.h"

#include "async/async.h"
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
#include "stcutil.h"
#include "util.h"

#include "stc/cstr.h"
#include "stc/zsview.h"

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

void fm_init(Fm *fm, struct lfm_opts *opts) {
  fm->paste.mode = PASTE_MODE_COPY;

  ev_timer_init(&fm->cursor_resting_timer, on_cursor_resting, 0,
                cfg.preview_delay / 1000.0);
  fm->cursor_resting_timer.data = to_lfm(fm);

  if (!cstr_is_empty(&opts->startpath)) {
    if (chdir(cstr_str(&opts->startpath)) != 0) {
      lfm_error(to_lfm(fm), "chdir: %s", strerror(errno));
    } else {
      fm->pwd = cstr_move(&opts->startpath);
    }
  } else {
    zsview pwd = getenv_zv("PWD");
    if (zsview_is_empty(pwd)) {
      char cwd[PATH_MAX + 1];
      if (getcwd(cwd, sizeof cwd) == NULL) {
        perror("getcwd");
        _exit(1);
      }
      fm->pwd = cstr_from(cwd);
    } else {
      fm->pwd = cstr_from_zv(pwd);
    }
  }

  int max = vec_int_size(&cfg.ratios);
  if (max > 1 && cfg.preview) {
    max--;
  }
  fm->dirs.max_visible = max;

  pathlist_init(&fm->selection.current);
  pathlist_init(&fm->selection.keep_in_visual);
  pathlist_init(&fm->selection.previous);
  pathlist_init(&fm->paste.buffer);

  fm_populate(fm);
  if (!cstr_is_empty(&opts->startfile)) {
    fm_move_cursor_to(fm, cstr_zv(&opts->startfile));
  }

  fm_update_watchers(fm);
  on_cursor_moved(fm, false);
}

void fm_deinit(Fm *fm) {
  vec_dir_drop(&fm->dirs.visible);
  pathlist_drop(&fm->selection.current);
  pathlist_drop(&fm->selection.keep_in_visual);
  pathlist_drop(&fm->selection.previous);
  pathlist_drop(&fm->paste.buffer);
  cstr_drop(&fm->automark);
  cstr_drop(&fm->pwd);
}

static void fm_populate(Fm *fm) {
  vec_dir_clear(&fm->dirs.visible);
  Dir *dir = loader_dir_from_path(&to_lfm(fm)->loader, cstr_zv(&fm->pwd),
                                  true); /* current dir */
  dir->visible = true;
  vec_dir_push_back(&fm->dirs.visible, dir);

  for (int i = 0; i < fm->dirs.max_visible - 1; i++) {
    zsview parent = path_parent(dir_path(dir));
    if (!zsview_is_empty(parent)) {
      dir = loader_dir_from_path(&to_lfm(fm)->loader, parent, true);
      dir->visible = true;
      vec_dir_push_back(&fm->dirs.visible, dir);
      if (dir_loading(dir)) {
        zsview name = *dir_name(*vec_dir_at(
            &fm->dirs.visible, vec_dir_size(&fm->dirs.visible) - 2));

        dir_cursor_move_to(dir, name, fm->height, cfg.scrolloff);
      }
    }
  }

  c_foreach(it, vec_dir, fm->dirs.visible) {
    if (*it.ref == dir)
      continue;
  }
}

void fm_recol(Fm *fm) {
  fm_remove_preview(fm);

  c_foreach(it, vec_dir, fm->dirs.visible) {
    (*it.ref)->visible = false;
  }

  int max = vec_int_size(&cfg.ratios);
  if (max > 1 && cfg.preview) {
    max--;
  }
  fm->dirs.max_visible = max;

  fm_populate(fm);
  fm_update_watchers(fm);
  on_cursor_moved(fm, false);
}

static inline bool fm_chdir_impl(Fm *fm, zsview path, bool save, bool hook,
                                 bool async) {
  char buf[PATH_MAX + 1];
  if (path_is_relative(path.str)) {
    // TODO: is there a reason why we don't use fm->pwd? 2025-05-21
    int len = snprintf(buf, sizeof buf, "%s/%s", getenv("PWD"), path.str);
    path = zsview_from_n(buf, len);
  }

  if (async) {
    async_chdir(&to_lfm(fm)->async, path.str, hook);
  } else {
    if (chdir(path.str) == 0) {
      setenv("PWD", path.str, true);
    } else {
      lfm_error(to_lfm(fm), "chdir: %s", strerror(errno));
      return false;
    }
  }

  notify_remove_watchers(&to_lfm(fm)->notify);

  cstr_assign_zv(&fm->pwd, path);

  if (save) {
    if (fm_current_dir(fm)->error) {
      cstr_clear(&fm->automark);
    } else {
      cstr_copy(&fm->automark, *dir_path(fm_current_dir(fm)));
    }
  }

  fm_remove_preview(fm);
  c_foreach(it, vec_dir, fm->dirs.visible) {
    (*it.ref)->visible = false;
  }

  fm_populate(fm);
  fm_update_watchers(fm);
  on_cursor_moved(fm, false);

  if (!async && hook) {
    lfm_run_hook(to_lfm(fm), LFM_HOOK_CHDIRPOST, &fm->pwd);
  }

  return true;
}

bool fm_sync_chdir(Fm *fm, zsview path, bool save, bool hook) {
  return fm_chdir_impl(fm, path, save, hook, false);
}

bool fm_async_chdir(Fm *fm, zsview path, bool save, bool hook) {
  return fm_chdir_impl(fm, path, save, hook, true);
}

static inline void fm_update_watchers(Fm *fm) {
  // watcher for preview is updated in update_preview
  notify_remove_watchers(&to_lfm(fm)->notify);
  c_foreach(it, vec_dir, fm->dirs.visible) {
    async_notify_add(&to_lfm(fm)->async, *it.ref);
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
    dir_cursor_move_to(dir, *file_name(file), fm->height, cfg.scrolloff);
  }
}

void fm_sort(Fm *fm) {
  c_foreach(it, vec_dir, fm->dirs.visible) {
    if (!dir_check(*it.ref)) {
      fm_sort_and_reselect(fm, *it.ref);
    }
  }
  fm_sort_and_reselect(fm, fm->dirs.preview);
}

void fm_hidden_set(Fm *fm, bool hidden) {
  cfg.dir_settings.hidden = hidden;
  fm_sort(fm);
  on_cursor_moved(fm, false);
}

void fm_check_dirs(const Fm *fm) {
  c_foreach(it, vec_dir, fm->dirs.visible) {
    if (!dir_check(*it.ref)) {
      loader_dir_reload(&to_lfm(fm)->loader, *it.ref);
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
  c_foreach(it, vec_dir, fm->dirs.visible) {
    async_dir_load(&to_lfm(fm)->async, *it.ref, true);
  }
  if (fm->dirs.preview) {
    async_dir_load(&to_lfm(fm)->async, fm->dirs.preview, true);
  }
}

static inline void fm_remove_preview(Fm *fm) {
  if (fm->dirs.preview) {
    log_trace("removing preview %s", dir_path_str(fm->dirs.preview));
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

  static uint64_t last_time_called = 0;
  uint64_t now = current_millis();
  if (delay_action) {
    // cursor was resting, don't delay
    if (now - last_time_called > cfg.preview_delay) {
      delay_action = false;
    }
  }
  last_time_called = now;

  log_trace("on_cursor_moved delay_action=%d", delay_action);

  if (!cfg.preview) {
    fm_remove_preview(fm);
    return;
  }

  const File *file = fm_current_file(fm);
  bool is_directory_preview = file != NULL && file_isdir(file);
  bool is_same_preview = file != NULL && fm->dirs.preview != NULL &&
                         cstr_eq(dir_path(fm->dirs.preview), file_path(file));

  if (!is_same_preview) {
    fm_remove_preview(fm);
  }

  if (is_directory_preview && !is_same_preview) {
    fm->dirs.preview = loader_dir_from_path(
        &to_lfm(fm)->loader, cstr_zv(file_path(file)), !delay_action);
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

static inline void fm_selection_toggle(Fm *fm, const cstr *path,
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

void fm_selection_add(Fm *fm, const cstr *path, bool run_hook) {
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
    pathlist_add(&fm->selection.keep_in_visual, it.ref);
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

void fm_selection_write(const Fm *fm, zsview path) {
  char buf[PATH_MAX + 1];
  if (path.size > PATH_MAX) {
    log_error("fm_selection_write: path too long");
    return;
  }

  memcpy(buf, path.str, path.size);
  buf[path.size] = 0;
  char *dir = dirname(buf);
  mkdir_p(dir, 755);

  FILE *fp = fopen(path.str, "w");
  if (!fp) {
    lfm_error(to_lfm(fm), "selfile: %s", strerror(errno));
    return;
  }

  if (pathlist_size(&fm->selection.current) > 0) {
    for (pathlist_iter it = pathlist_begin(&fm->selection.current); it.ref;
         pathlist_next(&it)) {
      fwrite(cstr_str(it.ref), 1, cstr_size(it.ref), fp);
      fputc('\n', fp);
    }
  } else {
    const File *file = fm_current_file(fm);
    if (file) {
      fputs(file_path_str(file), fp);
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
  pathlist_drop(&fm->paste.buffer);
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

void fm_move_cursor_to(Fm *fm, zsview name) {
  dir_cursor_move_to(fm_current_dir(fm), name, fm->height, cfg.scrolloff);
  on_cursor_moved(fm, false);
}

void fm_move_cursor_to_ptr(Fm *fm, const File *file) {
  Dir *d = fm_current_dir(fm);
  for (uint32_t i = 0; i < d->length; i++) {
    if (d->files[i] == file) {
      dir_cursor_move(d, i - d->ind, fm->height, cfg.scrolloff);
      break;
    }
  }
  d->ind = min(d->ind, d->length);
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

  fm_async_chdir(fm, cstr_zv(file_path(file)), false, true);
  return NULL;
}

/* TODO: allow updir into directories that don't exist so we can move out of
 * deleted directories (on 2021-11-18) */
bool fm_updir(Fm *fm) {
  if (dir_isroot(fm_current_dir(fm))) {
    return false;
  }

  zsview path = path_parent(dir_path(fm_current_dir(fm)));
  fm_async_chdir(fm, path, false, true);
  on_cursor_moved(fm, false);
  return true;
}

void fm_filter(Fm *fm, Filter *filter) {
  Dir *dir = fm_current_dir(fm);
  File *file = dir_current_file(dir);
  dir_filter(dir, filter);
  dir_cursor_move_to(dir, file ? *file_name(file) : c_zv(""), fm->height,
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
