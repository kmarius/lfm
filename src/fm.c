#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "fm.h"
#include "hashtab.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "notify.h"
#include "util.h"

static void fm_update_watchers(Fm *fm);
static void fm_remove_preview(Fm *fm);
static void fm_populate(Fm *fm);

void fm_init(Fm *fm, struct lfm_s *lfm)
{
  fm->paste.mode = PASTE_MODE_COPY;
  fm->lfm = lfm;

  if (cfg.startpath) {
    if (chdir(cfg.startpath) != 0) {
      lfm_error(fm->lfm, "chdir: %s", strerror(errno));
    } else {
      fm->pwd = strdup(cfg.startpath);
    }
  } else {
    const char *s = getenv("PWD");
    if (s) {
      fm->pwd = strdup(s);
    } else {
      char pwd[PATH_MAX];
      getcwd(pwd, sizeof pwd);
      fm->pwd = strdup(pwd);
    }
  }

  fm->dirs.length = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
  cvector_grow(fm->dirs.visible, fm->dirs.length);

  fm->selection.paths = lht_create(free);
  fm->selection.previous = ht_create(NULL);
  fm->paste.buffer = lht_create(free);

  fm_populate(fm);

  fm_update_watchers(fm);

  if (cfg.startfile) {
    fm_move_cursor_to(fm, cfg.startfile);
  }

  fm_update_preview(fm);

}


void fm_deinit(Fm *fm)
{
  cvector_free(fm->dirs.visible);
  lht_destroy(fm->selection.paths);
  ht_destroy(fm->selection.previous);
  lht_destroy(fm->paste.buffer);
  free(fm->automark);
  free(fm->find_prefix);
  free(fm->pwd);
}


static void fm_populate(Fm *fm)
{
  fm->dirs.visible[0] = loader_dir_from_path(&fm->lfm->loader, fm->pwd); /* current dir */
  fm->dirs.visible[0]->visible = true;
  Dir *d = fm_current_dir(fm);
  for (uint32_t i = 1; i < fm->dirs.length; i++) {
    const char *s = dir_parent_path(d);
    if (s) {
      d = loader_dir_from_path(&fm->lfm->loader, s);
      d->visible = true;
      fm->dirs.visible[i] = d;
      dir_cursor_move_to(d, fm->dirs.visible[i-1]->name, fm->height, cfg.scrolloff);
    } else {
      fm->dirs.visible[i] = NULL;
    }
  }
}


void fm_recol(Fm *fm)
{
  fm_remove_preview(fm);
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible[i]) {
      fm->dirs.visible[i]->visible = false;
    }
  }

  const uint32_t l = max(1, cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0));
  cvector_grow(fm->dirs.visible, l);
  cvector_set_size(fm->dirs.visible, l);
  fm->dirs.length = l;

  fm_populate(fm);
  fm_update_watchers(fm);
  fm_update_preview(fm);
}


bool fm_chdir(Fm *fm, const char *path, bool save, bool hook)
{
  fm_selection_visual_stop(fm);

  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  async_chdir(&fm->lfm->async, path, hook);

  notify_remove_watchers(&fm->lfm->notify);

  free(fm->pwd);
  fm->pwd = strdup(path);

  if (save) {
    free(fm->automark);
    fm->automark = fm_current_dir(fm)->error
      ? NULL
      : strdup(fm_current_dir(fm)->path);
  }

  fm_remove_preview(fm);
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible[i]) {
      fm->dirs.visible[i]->visible = false;
    }
  }

  fm_populate(fm);
  fm_update_watchers(fm);
  fm_update_preview(fm);

  return true;
}


static inline void fm_update_watchers(Fm *fm)
{
  // watcher for preview is updated in update_preview
  notify_remove_watchers(&fm->lfm->notify);
  for (size_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible[i]) {
      async_notify_add(&fm->lfm->async, fm->dirs.visible[i]);
    }
  }
}


/* TODO: maybe we can select the closest non-hidden file in case the
 * current one will be hidden (on 2021-10-17) */
static inline void fm_sort_and_reselect(Fm *fm, Dir *dir)
{
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


void fm_sort(Fm *fm)
{
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    fm_sort_and_reselect(fm, fm->dirs.visible[i]);
  }
  fm_sort_and_reselect(fm, fm->dirs.preview);
}


void fm_hidden_set(Fm *fm, bool hidden)
{
  cfg.dir_settings.hidden = hidden;
  fm_sort(fm);
  fm_update_preview(fm);
}


void fm_check_dirs(const Fm *fm)
{
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible[i] && !dir_check(fm->dirs.visible[i])) {
      loader_dir_reload(&fm->lfm->loader, fm->dirs.visible[i]);
    }
  }

  if (fm->dirs.preview && !dir_check(fm->dirs.preview)) {
    loader_dir_reload(&fm->lfm->loader, fm->dirs.preview);
  }
}


void fm_drop_cache(Fm *fm)
{
  log_debug("dropping directory cache");

  notify_remove_watchers(&fm->lfm->notify);
  fm_remove_preview(fm);

  loader_drop_dir_cache(&fm->lfm->loader);

  fm_populate(fm);
  fm_update_preview(fm);
  fm_update_watchers(fm);
}


void fm_reload(Fm *fm)
{
  for (uint32_t i = 0; i < fm->dirs.length; i++) {
    if (fm->dirs.visible[i]) {
      async_dir_load(&fm->lfm->async, fm->dirs.visible[i], true);
    }
  }
  if (fm->dirs.preview) {
    async_dir_load(&fm->lfm->async, fm->dirs.preview, true);
  }
}


static inline void fm_remove_preview(Fm *fm)
{
  if (!fm->dirs.preview) {
    return;
  }

  notify_remove_watcher(&fm->lfm->notify, fm->dirs.preview);
  fm->dirs.preview->visible = false;
  fm->dirs.preview = NULL;
}


// TODO: (on 2022-03-06)
// We should set the watcher for the preview directory with a delay, i.e. after
// on the directory for a second or so. This should greatly increase
// responsiveness when scrolling through directories on a slow device (setting
// watchers can be slow)
void fm_update_preview(Fm *fm)
{
  if (!cfg.preview) {
    fm_remove_preview(fm);
    return;
  }

  const File *file = fm_current_file(fm);
  if (file && file_isdir(file)) {
    if (fm->dirs.preview) {
      if (streq(fm->dirs.preview->path, file_path(file))) {
        return;
      }

      /* don'fm remove watcher if it is a currently visible (non-preview) dir */
      uint32_t i;
      for (i = 0; i < fm->dirs.length; i++) {
        if (fm->dirs.visible[i] && streq(fm->dirs.preview->path, fm->dirs.visible[i]->path)) {
          break;
        }
      }
      if (i >= fm->dirs.length) {
        notify_remove_watcher(&fm->lfm->notify, fm->dirs.preview);
        fm->dirs.preview->visible = false;
      }
    }
    fm->dirs.preview = loader_dir_from_path(&fm->lfm->loader, file_path(file));
    fm->dirs.preview->visible = true;
    async_notify_preview_add(&fm->lfm->async, fm->dirs.preview);
  } else {
    // file preview or empty
    if (fm->dirs.preview) {
      uint32_t i;
      for (i = 0; i < fm->dirs.length; i++) {
        if (fm->dirs.visible[i] && streq(fm->dirs.preview->path, fm->dirs.visible[i]->path)) {
          break;
        }
      }
      if (i == fm->dirs.length) {
        notify_remove_watcher(&fm->lfm->notify, fm->dirs.preview);
        fm->dirs.preview->visible = false;
      }
      fm->dirs.preview = NULL;
    }
  }
}

/* selection {{{ */


static inline void fm_selection_toggle(Fm *fm, const char *path)
{
  if (!lht_delete(fm->selection.paths, path)) {
    fm_selection_add(fm, path);
  }
}


void fm_selection_toggle_current(Fm *fm)
{
  if (fm->visual.active) {
    return;
  }
  File *file = fm_current_file(fm);
  if (file) {
    fm_selection_toggle(fm, file_path(file));
  }
}


void fm_selection_reverse(Fm *fm)
{
  const Dir *dir = fm_current_dir(fm);
  for (uint32_t i = 0; i < dir->length; i++) {
    fm_selection_toggle(fm, file_path(dir->files[i]));
  }
}


void fm_selection_visual_start(Fm *fm)
{
  if (fm->visual.active) {
    return;
  }

  Dir *dir = fm_current_dir(fm);
  if (dir->length == 0) {
    return;
  }

  /* TODO: what actually happens if we change sortoptions while visual is
   * active? (on 2021-11-15) */
  fm->visual.active = true;
  fm->visual.anchor = dir->ind;
  fm_selection_add(fm, file_path(dir->files[dir->ind]));
  ht_clear(fm->selection.previous);
  lht_foreach(char* path, fm->selection.paths) {
    ht_set(fm->selection.previous, path, path);
  }
}


void fm_selection_visual_stop(Fm *fm)
{
  if (!fm->visual.active) {
    return;
  }

  fm->visual.active = false;
  fm->visual.anchor = 0;
  ht_clear(fm->selection.previous);
}


void fm_selection_visual_toggle(Fm *fm)
{
  if (fm->visual.active) {
    fm_selection_visual_stop(fm);
  } else {
    fm_selection_visual_start(fm);
  }
}


static void selection_visual_update(Fm *fm, uint32_t origin, uint32_t from, uint32_t to)
{
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
    if (!ht_get(fm->selection.previous, file_path(dir->files[lo]))) {
      fm_selection_toggle(fm, file_path(dir->files[lo]));
    }
  }
}


void fm_selection_write(const Fm *fm, const char *path)
{
  char *dir, *buf = strdup(path);
  dir = dirname(buf);
  mkdir_p(dir, 755);
  free(buf);

  FILE *fp = fopen(path, "w");
  if (!fp) {
    lfm_error(fm->lfm, "selfile: %s", strerror(errno));
    return;
  }

  if (fm->selection.paths->size > 0) {
    lht_foreach(char *path, fm->selection.paths) {
      fputs(path, fp);
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

/* }}} */

/* load/copy/move {{{ */

/* TODO: Make it possible to append to cut/copy buffer (on 2021-07-25) */
void fm_paste_mode_set(Fm *fm, paste_mode mode)
{
  fm_selection_visual_stop(fm);
  fm->paste.mode = mode;
  if (fm->selection.paths->size == 0) {
    fm_selection_toggle_current(fm);
  }
  lht_destroy(fm->paste.buffer);
  fm->paste.buffer = fm->selection.paths;
  fm->selection.paths = lht_create(free);
}

/* }}} */

/* navigation {{{ */

bool fm_cursor_move(Fm *fm, int32_t ct)
{
  Dir *dir = fm_current_dir(fm);
  const uint32_t cur = dir->ind;
  dir_cursor_move(dir, ct, fm->height, cfg.scrolloff);
  if (dir->ind != cur) {
    if (fm->visual.active) {
      selection_visual_update(fm, fm->visual.anchor, cur, dir->ind);
    }
    fm_update_preview(fm);
  }
  return dir->ind != cur;
}


void fm_move_cursor_to(Fm *fm, const char *name)
{
  dir_cursor_move_to(fm_current_dir(fm), name, fm->height, cfg.scrolloff);
  fm_update_preview(fm);
}


bool fm_scroll_up(Fm *fm)
{
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
    fm_update_preview(fm);
  }
  return true;
}


bool fm_scroll_down(Fm *fm)
{
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
    fm_update_preview(fm);
  }
  return true;
}


File *fm_open(Fm *fm)
{
  File *file = fm_current_file(fm);
  if (!file) {
    return NULL;
  }

  fm_selection_visual_stop(fm);
  if (!file_isdir(file)) {
    return file;
  }

  fm_chdir(fm, file_path(file), false, false);
  return NULL;
}


/* TODO: allow updir into directories that don'fm exist so we can move out of
 * deleted directories (on 2021-11-18) */
bool fm_updir(Fm *fm)
{
  if (dir_isroot(fm_current_dir(fm))) {
    return false;
  }

  const char *name = fm_current_dir(fm)->name;
  fm_chdir(fm, dir_parent_path(fm_current_dir(fm)), false, false);
  fm_move_cursor_to(fm, name);
  fm_update_preview(fm);
  return true;
}

/* }}} */

/* filter {{{ */

void fm_filter(Fm *fm, const char *filter)
{
  Dir *dir = fm_current_dir(fm);
  File *file = dir_current_file(dir);
  dir_filter(dir, filter);
  dir_cursor_move_to(dir, file ? file_name(file) : NULL, fm->height, cfg.scrolloff);
  fm_update_preview(fm);
}

/* }}} */

/* TODO: To reload flattened directories, more notify watchers are needed (on 2022-02-06) */
void fm_flatten(Fm *fm, uint32_t level)
{
  fm_current_dir(fm)->flatten_level = level;
  async_dir_load(&fm->lfm->async, fm_current_dir(fm), true);
}


void fm_resize(Fm *fm, uint32_t height)
{
  uint32_t scrolloff = cfg.scrolloff;
  if (height < cfg.scrolloff * 2) {
    scrolloff = height / 2;
  }

  // is there a way to restore the position when just undoing a previous resize?
  ht_foreach(Dir *dir, fm->lfm->loader.dir_cache) {
    if (height > fm->height) {
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
      uint32_t scrolloff_bot = dir->length - dir->ind;
      if (scrolloff_bot > scrolloff) {
        scrolloff_bot = scrolloff;
      }
      if (dir->length > height && height - dir->pos < scrolloff_bot) {
        dir->pos = height - scrolloff_bot;
      }
    }
  }

  fm->height = height;
}
