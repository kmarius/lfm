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
#include "lualfm.h"
#include "notify.h"
#include "util.h"

#define T Fm

static void fm_update_watchers(T *t);
static void fm_remove_preview(T *t);
static void fm_populate(T *t);


void fm_init(T *t, struct lfm_s *lfm)
{
  t->paste.mode = PASTE_MODE_COPY;
  t->lfm = lfm;

  if (cfg.startpath) {
    if (chdir(cfg.startpath) != 0) {
      error("chdir: %s", strerror(errno));
    } else {
      setenv("PWD", cfg.startpath, true);
    }
  }

  t->dirs.length = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
  cvector_grow(t->dirs.visible, t->dirs.length);

  t->selection.paths = lht_create(free);
  t->selection.previous = ht_create(NULL);
  t->paste.buffer = lht_create(free);

  fm_populate(t);

  fm_update_watchers(t);

  if (cfg.startfile) {
    fm_move_cursor_to(t, cfg.startfile);
  }

  fm_update_preview(t);
}


void fm_deinit(T *t)
{
  cvector_free(t->dirs.visible);
  lht_destroy(t->selection.paths);
  ht_destroy(t->selection.previous);
  lht_destroy(t->paste.buffer);
  free(t->automark);
  free(t->find_prefix);
}


static void fm_populate(T *t)
{
  char pwd[PATH_MAX];

  const char *s = getenv("PWD");
  if (s) {
    strncpy(pwd, s, sizeof pwd - 1);
  } else {
    getcwd(pwd, sizeof pwd);
  }

  t->dirs.visible[0] = loader_dir_from_path(&t->lfm->loader, pwd); /* current dir */
  t->dirs.visible[0]->visible = true;
  Dir *d = fm_current_dir(t);
  for (uint32_t i = 1; i < t->dirs.length; i++) {
    if ((s = dir_parent_path(d))) {
      d = loader_dir_from_path(&t->lfm->loader, s);
      d->visible = true;
      t->dirs.visible[i] = d;
      dir_cursor_move_to(d, t->dirs.visible[i-1]->name, t->height, cfg.scrolloff);
    } else {
      t->dirs.visible[i] = NULL;
    }
  }
}


void fm_recol(T *t)
{
  fm_remove_preview(t);
  for (uint32_t i = 0; i < t->dirs.length; i++) {
    if (t->dirs.visible[i]) {
      t->dirs.visible[i]->visible = false;
    }
  }

  const uint32_t l = max(1, cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0));
  cvector_grow(t->dirs.visible, l);
  cvector_set_size(t->dirs.visible, l);
  t->dirs.length = l;

  fm_populate(t);
  fm_update_watchers(t);
  fm_update_preview(t);
}


bool fm_chdir(T *t, const char *path, bool save)
{
  fm_selection_visual_stop(t);

  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  uint64_t t0 = current_millis();
  if (chdir(path) != 0) {

    uint64_t t1 = current_millis();
    if (t1 - t0 > 10) {
      log_debug("chdir(\"%s\") took %ums", path, t1-t0);
    }

    error("chdir: %s", strerror(errno));
    return false;
  }

  uint64_t t1 = current_millis();
  if (t1 - t0 > 10) {
    log_debug("chdir(\"%s\") took %ums", path, t1-t0);
  }

  notify_set_watchers(&t->lfm->notify, NULL, 0);

  setenv("PWD", path, true);

  if (save) {
    free(t->automark);
    t->automark = fm_current_dir(t)->error
      ? NULL
      : strdup(fm_current_dir(t)->path);
  }

  fm_remove_preview(t);
  for (uint32_t i = 0; i < t->dirs.length; i++) {
    if (t->dirs.visible[i]) {
      t->dirs.visible[i]->visible = false;
    }
  }

  fm_populate(t);
  fm_update_watchers(t);
  fm_update_preview(t);

  return true;
}


static inline void fm_update_watchers(T *t)
{
  // watcher for preview is updated in update_preview
  notify_set_watchers(&t->lfm->notify, t->dirs.visible, t->dirs.length);
}


/* TODO: maybe we can select the closest non-hidden file in case the
 * current one will be hidden (on 2021-10-17) */
static inline void fm_sort_and_reselect(T *t, Dir *dir)
{
  if (!dir) {
    return;
  }

  dir->hidden = cfg.hidden;
  const File *file = dir_current_file(dir);
  dir_sort(dir);
  if (file) {
    dir_cursor_move_to(dir, file_name(file), t->height, cfg.scrolloff);
  }
}


void fm_sort(T *t)
{
  for (uint32_t i = 0; i < t->dirs.length; i++) {
    fm_sort_and_reselect(t, t->dirs.visible[i]);
  }
  fm_sort_and_reselect(t, t->dirs.preview);
}


void fm_hidden_set(T *t, bool hidden)
{
  cfg.hidden = hidden;
  fm_sort(t);
  fm_update_preview(t);
}


void fm_check_dirs(const T *t)
{
  for (uint32_t i = 0; i < t->dirs.length; i++) {
    if (t->dirs.visible[i] && !dir_check(t->dirs.visible[i])) {
      loader_dir_reload(&t->lfm->loader, t->dirs.visible[i]);
    }
  }

  if (t->dirs.preview && !dir_check(t->dirs.preview)) {
    loader_dir_reload(&t->lfm->loader, t->dirs.preview);
  }
}


void fm_drop_cache(T *t)
{
  notify_set_watchers(&t->lfm->notify, NULL, 0);

  log_debug("dropping cache");
  fm_remove_preview(t);

  loader_drop_dir_cache(&t->lfm->loader);

  fm_populate(t);
  fm_update_preview(t);
  fm_update_watchers(t);
}


void fm_reload(T *t)
{
  for (uint32_t i = 0; i < t->dirs.length; i++) {
    if (t->dirs.visible[i]) {
      async_dir_load(&t->lfm->async, t->dirs.visible[i], true);
    }
  }
  if (t->dirs.preview) {
    async_dir_load(&t->lfm->async, t->dirs.preview, true);
  }
}


static void fm_remove_preview(T *t)
{
  if (!t->dirs.preview) {
    return;
  }

  notify_remove_watcher(&t->lfm->notify, t->dirs.preview);
  t->dirs.preview->visible = false;
  t->dirs.preview = NULL;
}


// TODO: (on 2022-03-06)
// We should set the watcher for the preview directory with a delay, i.e. after
// on the directory for a second or so. This should greatly increase
// responsiveness when scrolling through directories on a slow device (setting
// watchers can be slow)
void fm_update_preview(T *t)
{
  if (!cfg.preview) {
    fm_remove_preview(t);
    return;
  }

  const File *file = fm_current_file(t);
  if (file && file_isdir(file)) {
    if (t->dirs.preview) {
      if (streq(t->dirs.preview->path, file_path(file))) {
        return;
      }

      /* don't remove watcher if it is a currently visible (non-preview) dir */
      uint32_t i;
      for (i = 0; i < t->dirs.length; i++) {
        if (t->dirs.visible[i] && streq(t->dirs.preview->path, t->dirs.visible[i]->path)) {
          break;
        }
      }
      if (i >= t->dirs.length) {
        notify_remove_watcher(&t->lfm->notify, t->dirs.preview);
        t->dirs.preview->visible = false;
      }
    }
    t->dirs.preview = loader_dir_from_path(&t->lfm->loader, file_path(file));
    t->dirs.preview->visible = true;
    // sometimes very slow on smb (> 200ms)
    notify_add_watcher(&t->lfm->notify, t->dirs.preview);
  } else {
    // file preview or empty
    if (t->dirs.preview) {
      uint32_t i;
      for (i = 0; i < t->dirs.length; i++) {
        if (t->dirs.visible[i] && streq(t->dirs.preview->path, t->dirs.visible[i]->path)) {
          break;
        }
      }
      if (i == t->dirs.length) {
        notify_remove_watcher(&t->lfm->notify, t->dirs.preview);
        t->dirs.preview->visible = false;
      }
      t->dirs.preview = NULL;
    }
  }
}

/* selection {{{ */


static inline void fm_selection_toggle(T *t, const char *path)
{
  if (!lht_delete(t->selection.paths, path)) {
    fm_selection_add(t, path);
  }
}


void fm_selection_toggle_current(T *t)
{
  if (t->visual.active) {
    return;
  }
  File *file = fm_current_file(t);
  if (file) {
    fm_selection_toggle(t, file_path(file));
  }
}


void fm_selection_reverse(T *t)
{
  const Dir *dir = fm_current_dir(t);
  for (uint32_t i = 0; i < dir->length; i++) {
    fm_selection_toggle(t, file_path(dir->files[i]));
  }
}


void fm_selection_visual_start(T *t)
{
  if (t->visual.active) {
    return;
  }

  Dir *dir = fm_current_dir(t);
  if (dir->length == 0) {
    return;
  }

  /* TODO: what actually happens if we change sortoptions while visual is
   * active? (on 2021-11-15) */
  t->visual.active = true;
  t->visual.anchor = dir->ind;
  fm_selection_add(t, file_path(dir->files[dir->ind]));
  ht_clear(t->selection.previous);
  lht_foreach(char* path, t->selection.paths) {
    ht_set(t->selection.previous, path, path);
  }
}


void fm_selection_visual_stop(T *t)
{
  if (!t->visual.active) {
    return;
  }

  t->visual.active = false;
  t->visual.anchor = 0;
  ht_clear(t->selection.previous);
}


void fm_selection_visual_toggle(T *t)
{
  if (t->visual.active) {
    fm_selection_visual_stop(t);
  } else {
    fm_selection_visual_start(t);
  }
}


static void selection_visual_update(T *t, uint32_t origin, uint32_t from, uint32_t to)
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
  const Dir *dir = fm_current_dir(t);
  for (; lo <= hi; lo++) {
    // never unselect the old selection
    if (!ht_get(t->selection.previous, file_path(dir->files[lo]))) {
      fm_selection_toggle(t, file_path(dir->files[lo]));
    }
  }
}


void fm_selection_write(const T *t, const char *path)
{
  char *dir, *buf = strdup(path);
  dir = dirname(buf);
  mkdir_p(dir, 755);
  free(buf);

  FILE *fp = fopen(path, "w");
  if (!fp) {
    error("selfile: %s", strerror(errno));
    return;
  }

  if (t->selection.paths->size > 0) {
    lht_foreach(char *path, t->selection.paths) {
      fputs(path, fp);
      fputc('\n', fp);
    }
  } else {
    const File *file = fm_current_file(t);
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
void fm_paste_mode_set(T *t, enum paste_mode_e mode)
{
  fm_selection_visual_stop(t);
  t->paste.mode = mode;
  if (t->selection.paths->size == 0) {
    fm_selection_toggle_current(t);
  }
  lht_destroy(t->paste.buffer);
  t->paste.buffer = t->selection.paths;
  t->selection.paths = lht_create(free);
}

/* }}} */

/* navigation {{{ */

bool fm_cursor_move(T *t, int32_t ct)
{
  Dir *dir = fm_current_dir(t);
  const uint32_t cur = dir->ind;
  dir_cursor_move(dir, ct, t->height, cfg.scrolloff);
  if (dir->ind != cur) {
    if (t->visual.active) {
      selection_visual_update(t, t->visual.anchor, cur, dir->ind);
    }
    fm_update_preview(t);
  }
  return dir->ind != cur;
}


void fm_move_cursor_to(T *t, const char *name)
{
  dir_cursor_move_to(fm_current_dir(t), name, t->height, cfg.scrolloff);
  fm_update_preview(t);
}


bool fm_scroll_up(T *t)
{
  Dir *dir = fm_current_dir(t);
  if (dir->ind > 0 && dir->ind == dir->pos) {
    return fm_up(t, 1);
  }
  if (dir->pos < t->height - cfg.scrolloff - 1) {
    dir->pos++;
  } else {
    dir->pos = t->height - cfg.scrolloff - 1;
    dir->ind--;
    if (dir->ind > dir->length - cfg.scrolloff - 1) {
      dir->ind = dir->length - cfg.scrolloff - 1;
    }
    fm_update_preview(t);
  }
  return true;
}


bool fm_scroll_down(T *t)
{
  Dir *dir = fm_current_dir(t);
  if (dir->length - dir->ind + dir->pos - 1 < t->height) {
    return fm_down(t, 1);
  }
  if (dir->pos > cfg.scrolloff) {
    dir->pos--;
  } else {
    dir->pos = cfg.scrolloff;
    dir->ind++;
    if (dir->ind < dir->pos) {
      dir->ind = dir->pos;
    }
    fm_update_preview(t);
  }
  return true;
}


File *fm_open(T *t)
{
  File *file = fm_current_file(t);
  if (!file) {
    return NULL;
  }

  fm_selection_visual_stop(t);
  if (!file_isdir(file)) {
    return file;
  }

  fm_chdir(t, file_path(file), false);
  return NULL;
}


/* TODO: allow updir into directories that don't exist so we can move out of
 * deleted directories (on 2021-11-18) */
bool fm_updir(T *t)
{
  if (dir_isroot(fm_current_dir(t))) {
    return false;
  }

  const char *name = fm_current_dir(t)->name;
  fm_chdir(t, dir_parent_path(fm_current_dir(t)), false);
  fm_move_cursor_to(t, name);
  fm_update_preview(t);
  return true;
}

/* }}} */

/* filter {{{ */

void fm_filter(T *t, const char *filter)
{
  Dir *dir = fm_current_dir(t);
  File *file = dir_current_file(dir);
  dir_filter(dir, filter);
  dir_cursor_move_to(dir, file ? file_name(file) : NULL, t->height, cfg.scrolloff);
  fm_update_preview(t);
}

/* }}} */

/* TODO: To reload flattened directories, more notify watchers are needed (on 2022-02-06) */
void fm_flatten(T *t, uint32_t level)
{
  fm_current_dir(t)->flatten_level = level;
  async_dir_load(&t->lfm->async, fm_current_dir(t), true);
}


void fm_resize(T *t, uint32_t height)
{
  uint32_t scrolloff = cfg.scrolloff;
  if (height < cfg.scrolloff * 2) {
    scrolloff = height / 2;
  }

  // is there a way to restore the position when just undoing a previous resize?
  ht_foreach(Dir *dir, t->lfm->loader.dir_cache) {
    if (height > t->height) {
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
    } else if (height < t->height) {
      uint32_t scrolloff_bot = dir->length - dir->ind;
      if (scrolloff_bot > scrolloff) {
        scrolloff_bot = scrolloff;
      }
      if (dir->length > height && height - dir->pos < scrolloff_bot) {
        dir->pos = height - scrolloff_bot;
      }
    }
  }

  t->height = height;
}
