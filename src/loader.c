#include "loader.h"
#include "async/async.h"
#include "config.h"
#include "dir.h"
#include "hooks.h"
#include "lfm.h"
#include "macros_defs.h"
#include "path.h"
#include "preview.h"
#include "stc/common.h"
#include "ui.h"
#include "util.h"

#include <ev.h>

#include <stddef.h>
#include <stdint.h>

#include "stc/cstr.h"
#include "stc/zsview.h"

#define i_declared
#define i_type previewcache
#define i_key zsview
#define i_val Preview *
#define i_valdrop(p) preview_destroy(*(p))
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_no_clone
#include "stc/hmap.h"

struct loader_timer {
  ev_timer watcher;
  Lfm *lfm;
  union {
    Preview *preview;
    Dir *dir;
  };
};

#define i_declared
#define i_type list_loader_timer, struct loader_timer
#include "stc/dlist.h"

#define i_TYPE set_dir, Dir *
#include "stc/hset.h"

#define i_TYPE set_preview, Preview *
#include "stc/hset.h"

void loader_init(Loader *loader) {
  loader->dc = dircache_init();
  loader->pc = previewcache_init();
}

void loader_deinit(Loader *loader) {
  list_loader_timer_drop(&loader->dir_timers);
  list_loader_timer_drop(&loader->preview_timers);
  dircache_drop(&loader->dc);
  previewcache_drop(&loader->pc);
}

static void dir_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct loader_timer *timer = (struct loader_timer *)w;
  Lfm *lfm = timer->lfm;
  async_dir_load(&lfm->async, timer->dir, true);
  timer->dir->loading = true;
  ev_timer_stop(EV_A_ w);
  list_loader_timer_erase_node(&lfm->loader.dir_timers,
                               list_loader_timer_get_node(timer));
}

static void pv_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct loader_timer *timer = (struct loader_timer *)w;
  Lfm *lfm = timer->lfm;
  async_preview_load(&lfm->async, timer->preview);
  ev_timer_stop(EV_A_ w);
  list_loader_timer_erase_node(&lfm->loader.preview_timers,
                               list_loader_timer_get_node(timer));
}

static inline void schedule_dir_load(Loader *loader, Dir *dir, uint64_t time) {
  Lfm *lfm = to_lfm(loader);
  struct loader_timer *timer =
      list_loader_timer_push(&loader->dir_timers, (struct loader_timer){
                                                      .dir = dir,
                                                      .lfm = lfm,
                                                  });
  ev_timer_init(&timer->watcher, dir_timer_cb, 0,
                (time - current_millis()) / 1000.);
  ev_timer_again(lfm->loop, &timer->watcher);
  dir->next_scheduled_load = time;
  dir->next_requested_load = 0;
  dir->scheduled = true;
}

static inline void schedule_preview_load(Loader *loader, Preview *pv,
                                         uint64_t time) {
  Lfm *lfm = to_lfm(loader);
  struct loader_timer *timer =
      list_loader_timer_push(&loader->preview_timers, (struct loader_timer){
                                                          .preview = pv,
                                                          .lfm = lfm,
                                                      });
  ev_timer_init(&timer->watcher, pv_timer_cb, 0,
                (time - current_millis()) / 1000.);
  ev_timer_again(lfm->loop, &timer->watcher);
}

void loader_dir_reload(Loader *loader, Dir *dir) {
  if (dir->scheduled) {
    return;
  }

  uint64_t now = current_millis();
  uint64_t latest = dir->next_scheduled_load;

  // Never schedule the same directory more than once. Once the update
  // of the directory is applied we will check if we need to load again.
  if (latest >= now + cfg.inotify_timeout) {
    return; // discard
  }

  // Add a (small) delay so we don't show files that exist only very briefly
  uint64_t next = now < latest + cfg.inotify_timeout
                      ? latest + cfg.inotify_timeout + cfg.inotify_delay
                      : now + cfg.inotify_delay;
  if (dir->loading) {
    dir->next_requested_load = next;
  } else {
    schedule_dir_load(loader, dir, next);
  }
}

void loader_dir_load_callback(Loader *loader, Dir *dir) {
  dir->scheduled = false;
  if (dir->next_requested_load > 0) {
    uint64_t now = current_millis();
    if (dir->next_requested_load <= now) {
      async_dir_load(&to_lfm(loader)->async, dir, true);
      dir->next_scheduled_load = now;
      dir->next_requested_load = 0;
      dir->loading = true;
    } else {
      schedule_dir_load(loader, dir, dir->next_requested_load);
    }
  }
}

void loader_preview_reload(Loader *loader, Preview *pv) {
  uint64_t now = current_millis();
  uint64_t latest = pv->next; // possibly in the future

  if (latest >= now + cfg.inotify_timeout) {
    return; // discard
  }

  // Add a small delay so we don't show files that exist only very briefly
  uint64_t next = now < latest + cfg.inotify_timeout
                      ? latest + cfg.inotify_timeout + cfg.inotify_delay
                      : now + cfg.inotify_delay;
  schedule_preview_load(loader, pv, next);
  pv->next = next;
}

// path must be absolute
Dir *loader_dir_from_path(Loader *loader, zsview path, bool do_load) {
  char buf[PATH_MAX + 1];

  // make sure there is no trailing / in path
  // truncates, in the unlikely case we got passed something longer than
  // PATH_MAX
  if (path.size > 1 && path.str[path.size - 1] == '/') {
    if (path.size + 1 > (ptrdiff_t)sizeof buf) {
      path.size = sizeof buf - 1;
    }
    // copy into into our buffer and remove trailing slash
    path.size--;
    memcpy(buf, path.str, path.size);
    buf[path.size] = 0;
    path.str = buf;
  }

  dircache_value *v = dircache_get_mut(&loader->dc, path);
  Dir *dir = v ? v->second : NULL;
  if (dir) {
    if (do_load) {
      if (dir->status == DIR_LOADING_DELAYED && do_load) {
        // delayed loading
        async_dir_load(&to_lfm(loader)->async, dir, false);
        dir->last_loading_action = current_millis();
        ui_start_loading_indicator_timer(&to_lfm(loader)->ui);
        return dir;
      }
      if (dir->status == DIR_LOADING_FULLY && do_load) {
        // don't check before we have actually loaded the directory
        // (in particular stat data which we compare)
        async_dir_check(&to_lfm(loader)->async, dir);
      }
      /* TODO: no (on 2022-10-09) */
      dir->settings.hidden = cfg.dir_settings.hidden;
      dir_sort(dir);
    }
  } else {
    dir = dir_create(path);
    hmap_dirsetting_value *v =
        hmap_dirsetting_get_mut(&cfg.dir_settings_map, path);
    struct dir_settings *s = v ? &v->second : &cfg.dir_settings;
    memcpy(&dir->settings, s, sizeof *s);
    dircache_insert(&loader->dc, cstr_zv(&dir->path), dir);
    if (do_load) {
      async_dir_load(&to_lfm(loader)->async, dir, false);
      dir->last_loading_action = current_millis();
      ui_start_loading_indicator_timer(&to_lfm(loader)->ui);
      dir->loading = true;
    }
    if (to_lfm(loader)->L) {
      lfm_run_hook(to_lfm(loader), LFM_HOOK_DIRLOADED, path);
    }
  }
  return dir;
}

Preview *loader_preview_from_path(Loader *loader, zsview path, bool do_load) {
  char fullpath[PATH_MAX + 1];
  if (path_is_relative(path.str)) {
    int len =
        snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path.str);
    path.str = fullpath;
    path.size = len;
  }

  previewcache_value *v = previewcache_get_mut(&loader->pc, path);
  Preview *pv;
  if (v) {
    // preview existing in cache
    pv = v->second;

    if (do_load) {
      if (pv->status == PV_LOADING_DELAYED) {
        async_preview_load(&to_lfm(loader)->async, pv);
        return pv;
      }
      if (pv->status == PV_LOADING_NORMAL) {
        if (pv->reload_height < (int)to_lfm(loader)->ui.preview.y ||
            pv->reload_width < (int)to_lfm(loader)->ui.preview.x) {
          async_preview_load(&to_lfm(loader)->async, pv);
        } else {
          async_preview_check(&to_lfm(loader)->async, pv);
        }
      }
    }
  } else {
    pv = preview_create_loading(path, to_lfm(loader)->ui.y,
                                to_lfm(loader)->ui.x);
    previewcache_insert(&loader->pc, cstr_zv(&pv->path), pv);
    if (do_load) {
      async_preview_load(&to_lfm(loader)->async, pv);
    }
  }
  return pv;
}

void loader_drop_preview_cache(Loader *loader) {
  loader->preview_cache_version++;
  previewcache_clear(&loader->pc);
  c_foreach(it, list_loader_timer, loader->preview_timers) {
    ev_timer_stop(to_lfm(loader)->loop, &it.ref->watcher);
  }
  list_loader_timer_clear(&loader->preview_timers);
}

void loader_drop_dir_cache(Loader *loader) {
  loader->dir_cache_version++;
  dircache_clear(&loader->dc);
  c_foreach(it, list_loader_timer, loader->dir_timers) {
    ev_timer_stop(to_lfm(loader)->loop, &it.ref->watcher);
  }
  list_loader_timer_clear(&loader->dir_timers);
}

void loader_reschedule(Loader *loader) {
  set_dir dirs = set_dir_init();
  c_foreach(it, list_loader_timer, loader->dir_timers) {
    set_dir_insert(&dirs, it.ref->dir);
    ev_timer_stop(to_lfm(loader)->loop, &it.ref->watcher);
  }
  list_loader_timer_clear(&loader->dir_timers);

  set_preview previews = set_preview_init();
  c_foreach(it, list_loader_timer, loader->preview_timers) {
    set_preview_insert(&previews, it.ref->preview);
    ev_timer_stop(to_lfm(loader)->loop, &it.ref->watcher);
  }
  list_loader_timer_clear(&loader->preview_timers);

  uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

  c_foreach(it, set_dir, dirs) {
    schedule_dir_load(loader, *it.ref, next);
  }
  c_foreach(it, set_preview, previews) {
    schedule_preview_load(loader, *it.ref, next);
  }

  set_dir_clear(&dirs);
  set_preview_clear(&previews);
}

Preview *loader_preview_get(Loader *loader, zsview path) {
  previewcache_value *v = previewcache_get_mut(&loader->pc, path);
  return v ? v->second : NULL;
}
