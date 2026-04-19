#include "fm.h"

#include "async/async.h"
#include "config.h"
#include "defs.h"
#include "dir.h"
#include "getpwd.h"
#include "hooks.h"
#include "inotify.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "path.h"
#include "pathlist.h"
#include "stcutil.h"
#include "util.h"

#include <ev.h>
#include <stc/cstr.h>
#include <stc/zsview.h>

#include <linux/limits.h>
#include <unistd.h>

static void fm_update_watchers(Fm *fm);
static void fm_remove_preview(Fm *fm);
static void fm_populate(Fm *fm);

void fm_init(Fm *fm, struct lfm_opts *opts) {
  fm->paste.mode = PASTE_MODE_COPY;

  if (!cstr_is_empty(&opts->startpath)) {
    if (chdir(cstr_str(&opts->startpath)) == 0) {
      fm->pwd = cstr_move(&opts->startpath);
      setpwd(cstr_str(&fm->pwd));
    } else {
      lfm_perror(to_lfm(fm), "chdir");
    }
  }

  if (cstr_is_empty(&fm->pwd)) {
    zsview pwd = getpwd_zv_manual_unlock();
    fm->pwd = cstr_from_zv(pwd);
    getpwd_unlock();
  }

  i32 max = vec_int_size(&cfg.ratios);
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
    Dir *dir = fm_current_dir(fm);
    dir_move_cursor_to_name(dir, cstr_zv(&opts->startfile));
  }

  fm_update_watchers(fm);
  fm_update_preview(fm);
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

  for (i32 i = 0; i < fm->dirs.max_visible - 1; i++) {
    zsview parent = path_parent(dir_path(dir));
    if (!zsview_is_empty(parent)) {
      dir = loader_dir_from_path(&to_lfm(fm)->loader, parent, true);
      dir->visible = true;
      vec_dir_push_back(&fm->dirs.visible, dir);
      if (dir_loading(dir)) {
        zsview name = dir_name(*vec_dir_at(
            &fm->dirs.visible, vec_dir_size(&fm->dirs.visible) - 2));

        dir_move_cursor_to_name(dir, name);
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

  i32 max = vec_int_size(&cfg.ratios);
  if (max > 1 && cfg.preview)
    max--;

  fm->dirs.max_visible = max;

  fm_populate(fm);
  fm_update_watchers(fm);
  fm_update_preview(fm);
}

static inline bool fm_chdir_impl(Fm *fm, zsview path, bool save, bool hook,
                                 bool async) {
  char buf[PATH_MAX + 1];
  if (path_is_relative(path.str)) {
    isize len = path_make_absolute(path, buf, sizeof buf);
    if (len < 0) {
      lfm_errorf(to_lfm(fm), "path too long");
      return false;
    }
    path = zsview_from_n(buf, len);
  }

  if (async) {
    async_chdir(&to_lfm(fm)->async, path.str, hook);
  } else {
    if (unlikely(chdir(path.str) != 0)) {
      lfm_perror(to_lfm(fm), "chdir");
      return false;
    }
    setpwd(path.str);
  }

  inotify_remove_watchers(&to_lfm(fm)->inotify);
  async_inotify_cancel(&to_lfm(fm)->async);

  cstr_assign_zv(&fm->pwd, path);

  if (save) {
    if (fm_current_dir(fm)->error) {
      cstr_clear(&fm->automark);
    } else {
      cstr_assign_zv(&fm->automark, dir_path(fm_current_dir(fm)));
    }
  }

  fm_remove_preview(fm);
  c_foreach(it, vec_dir, fm->dirs.visible) {
    (*it.ref)->visible = false;
  }

  fm_populate(fm);
  fm_update_watchers(fm);

  if (!async && hook) {
    LFM_RUN_HOOK(to_lfm(fm), LFM_HOOK_CHDIRPOST, &fm->pwd);
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
  inotify_remove_watchers(&to_lfm(fm)->inotify);
  async_inotify_cancel(&to_lfm(fm)->async);
  c_foreach(it, vec_dir, fm->dirs.visible) {
    async_inotify_add(&to_lfm(fm)->async, *it.ref);
  }
}

/* TODO: maybe we can select the closest non-hidden file in case the
 * current one will be hidden (on 2021-10-17) */
static inline void sort_and_reselect(Fm *fm, Dir *dir) {
  (void)fm;
  if (unlikely(dir == NULL))
    return;

  dir->settings.hidden = cfg.dir_settings.hidden;
  File *file = dir_current_file(dir);
  dir_sort(dir, false);
  dir_move_cursor_to_ptr(dir, file);
}

void fm_sort(Fm *fm) {
  c_foreach(it, vec_dir, fm->dirs.visible) {
    sort_and_reselect(fm, *it.ref);
  }
  sort_and_reselect(fm, fm->dirs.preview);
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

  inotify_remove_watchers(&to_lfm(fm)->inotify);
  async_inotify_cancel(&to_lfm(fm)->async);
  async_dir_cancel(&to_lfm(fm)->async);
  fm_remove_preview(fm);
  loader_drop_dir_cache(&to_lfm(fm)->loader);

  fm_populate(fm);
  fm_update_watchers(fm);
  fm_update_preview(fm);
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
    inotify_remove_watcher(&to_lfm(fm)->inotify, fm->dirs.preview);
    fm->dirs.preview->visible = false;
    fm->dirs.preview = NULL;
  }
}

void fm_update_preview(Fm *fm) {
  if (!cfg.preview) {
    fm_remove_preview(fm);
    return;
  }

  Lfm *lfm = to_lfm(fm);

  File *file = fm_current_file(fm);
  bool is_directory_preview = file != NULL && file_isdir(file);
  if (!is_directory_preview) {
    fm_remove_preview(fm);
    return;
  }

  bool preview_changed =
      file == NULL || fm->dirs.preview == NULL ||
      !zsview_eq2(dir_path(fm->dirs.preview), file_path(file));

  if (preview_changed) {
    Dir *dir =
        loader_dir_from_path(&to_lfm(fm)->loader, file_path(file), false);
    fm->dirs.preview = dir;
    dir->visible = true;

    if (dir->status != DIR_DELAYED)
      async_dir_check(&lfm->async, dir);
    async_inotify_add_previewed(&lfm->async, lfm->fm.dirs.preview);
  }

  Dir *dir = fm->dirs.preview;
  if (dir && dir->status == DIR_DELAYED)
    async_dir_load(&lfm->async, lfm->fm.dirs.preview, false);
}

File *fm_open(Fm *fm) {
  File *file = fm_current_file(fm);
  if (!file)
    return NULL;

  if (!file_isdir(file))
    return file;

  fm_async_chdir(fm, file_path(file), false, true);
  return NULL;
}

/* TODO: allow updir into directories that don't exist so we can move out of
 * deleted directories (on 2021-11-18) */
bool fm_updir(Fm *fm) {
  if (dir_is_root(fm_current_dir(fm)))
    return false;

  Dir *dir = fm_current_dir(fm);
  zsview path = path_parent(dir_path(dir));
  fm_async_chdir(fm, path, false, true);
  Dir *parent = fm_current_dir(fm);
  dir_move_cursor_to_name(parent, dir_name(dir));
  fm_update_preview(fm);
  return true;
}

void fm_on_resize(Fm *fm, u32 height) {
  u32 scrolloff = cfg.scrolloff;
  if (height < cfg.scrolloff * 2)
    scrolloff = height / 2;

  // update height/scrolloff and adjust cursor position for
  // all loaded directories
  c_foreach(v, map_zsview_dir, to_lfm(fm)->loader.dir_cache) {
    Dir *dir = v.ref->second;
    dir->height = height;
    dir->scrolloff = scrolloff;

    usize len = dir_length(dir);

    if (height >= fm->height) {
      // terminal grew
      u32 scrolloff_top = dir->ind;
      if (scrolloff_top > scrolloff)
        scrolloff_top = scrolloff;

      if (len + dir->pos < height + dir->ind)
        dir->pos = height - len + dir->ind;
      if (len > height && dir->pos < scrolloff_top)
        dir->pos = scrolloff_top;

    } else if (height < fm->height) {
      // terminal shrinked
      u32 scrolloff = cfg.scrolloff;
      if (height < scrolloff * 2) {
        scrolloff = height / 2;
      }
      if (scrolloff >= len - dir->ind) {
        // closer to the end of directory than scrolloff
        dir->pos = height - (len - dir->ind);
      } else if (dir->pos + scrolloff >= height) {
        dir->pos = height - scrolloff - 1;
      }
    }

    // applies scrolloff for the directory
    dir_move_cursor(dir, 0);
  }
  fm->height = height;
}
