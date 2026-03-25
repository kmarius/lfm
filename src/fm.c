#include "fm.h"

#include "async/async.h"
#include "config.h"
#include "defs.h"
#include "dir.h"
#include "getpwd.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "loop.h"
#include "notify.h"
#include "path.h"
#include "pathlist.h"
#include "stcutil.h"
#include "util.h"

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <ev.h>
#include <stdint.h>

#include <libgen.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void on_cursor_resting(EV_P_ ev_timer *w, i32 revents);
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
      lfm_perror(to_lfm(fm), "chdir");
    } else {
      fm->pwd = cstr_move(&opts->startpath);
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
    dir_move_cursor_to_name(dir, cstr_zv(&opts->startfile), fm->height,
                            cfg.scrolloff);
  }

  fm_update_watchers(fm);
  fm_update_preview(fm, true);
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

        dir_move_cursor_to_name(dir, name, fm->height, cfg.scrolloff);
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
  fm_update_preview(fm, true);
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
    if (chdir(path.str) == 0) {
      setpwd(path.str);
    } else {
      lfm_perror(to_lfm(fm), "chdir");
      return false;
    }
  }

  notify_remove_watchers(&to_lfm(fm)->notify);

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
  fm_update_preview(fm, true);

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
static inline void sort_and_reselect(Fm *fm, Dir *dir) {
  if (!dir)
    return;

  /* TODO: shouldn't apply the global hidden setting (on 2022-10-09) */
  dir->settings.hidden = cfg.dir_settings.hidden;
  File *file = dir_current_file(dir);
  dir_sort(dir, false);
  dir_move_cursor_to_ptr(dir, file, fm->height, cfg.scrolloff);
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

  notify_remove_watchers(&to_lfm(fm)->notify);
  fm_remove_preview(fm);

  loader_drop_dir_cache(&to_lfm(fm)->loader);

  fm_populate(fm);
  fm_update_watchers(fm);
  fm_update_preview(fm, true);
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

// Loads the directory, if not done already, and adds an inotify watcher.
// Pass revents == 0 when calling manually to indicate we have already loaded
// the directory.
static void on_cursor_resting(EV_P_ ev_timer *w, i32 revents) {
  ev_timer_stop(loop, w);

  Lfm *lfm = w->data;

  Dir *dir = lfm->fm.dirs.preview;
  if (dir) {
    if (revents != 0) { // ev calls this with revents == 256, we pass 0
      if (dir->status == DIR_LOADING_DELAYED) {
        async_dir_load(&lfm->async, lfm->fm.dirs.preview, false);
      } else {
        async_dir_check(&lfm->async, dir);
      }
    }
    async_notify_preview_add(&lfm->async, lfm->fm.dirs.preview);
  }
}

void fm_update_preview(Fm *fm, bool immediate) {
  immediate |= cfg.preview_delay == 0;

  static u64 last_time_called = 0;
  u64 now = current_millis();
  if (!immediate) {
    if (now - last_time_called > cfg.preview_delay)
      immediate = true; // cursor was resting, don't delay
  }
  last_time_called = now;

  if (!cfg.preview) {
    fm_remove_preview(fm);
    return;
  }

  const File *file = fm_current_file(fm);
  bool is_directory_preview = file != NULL && file_isdir(file);
  bool preview_changed =
      file == NULL || fm->dirs.preview == NULL ||
      !zsview_eq2(dir_path(fm->dirs.preview), file_path(file));

  if (preview_changed)
    fm_remove_preview(fm);

  if (is_directory_preview && preview_changed) {
    fm->dirs.preview =
        loader_dir_from_path(&to_lfm(fm)->loader, file_path(file), immediate);
    fm->dirs.preview->visible = true;
  }

  if (preview_changed) {
    // invoke on_cursor_resting (on delay) to set up watcher/actually load the
    // directory

    if (immediate) {
      if (file)
        ev_invoke(event_loop, &fm->cursor_resting_timer, 0);
    } else {
      ev_timer_again(event_loop, &fm->cursor_resting_timer);
    }
  }
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
  dir_move_cursor_to_name(dir, dir_name(dir), fm->height, cfg.scrolloff);
  fm_update_preview(fm, true);
  return true;
}

void fm_on_resize(Fm *fm, u32 height) {
  u32 scrolloff = cfg.scrolloff;
  if (height < cfg.scrolloff * 2) {
    scrolloff = height / 2;
  }

  // TODO: is there a way to restore the position when just undoing a previous
  // resize?
  c_foreach(v, dircache, to_lfm(fm)->loader.dc) {
    Dir *dir = v.ref->second;
    if (height > fm->height) {
      // terminal grew
      u32 scrolloff_top = dir->ind;
      if (scrolloff_top > scrolloff) {
        scrolloff_top = scrolloff;
      }
      if (dir_length(dir) + dir->pos < height + dir->ind) {
        dir->pos = height - dir_length(dir) + dir->ind;
      }
      if (dir_length(dir) > height && dir->pos < scrolloff_top) {
        dir->pos = scrolloff_top;
      }
    } else if (height < fm->height) {
      // terminal shrinked
      u32 scrolloff = cfg.scrolloff;
      if (height < scrolloff * 2) {
        scrolloff = height / 2;
      }
      if (scrolloff >= dir_length(dir) - dir->ind) {
        // closer to the end of directory than scrolloff
        dir->pos = height - (dir_length(dir) - dir->ind);
      } else if (dir->pos + scrolloff >= height) {
        dir->pos = height - scrolloff - 1;
      }
    }
  }

  fm->height = height;
}
