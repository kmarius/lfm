#include "loader.h"
#include "async/async.h"
#include "config.h"
#include "defs.h"
#include "dir.h"
#include "hooks.h"
#include "lfm.h"
#include "loop.h"
#include "path.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <stc/common.h>
#include <stc/cstr.h>
#include <stc/zsview.h>

#include <stddef.h>

// key is zsview of preview->path and owned by dir
#define i_declared
#define i_type map_zsview_preview
#define i_key zsview
#define i_val Preview *
#define i_valdrop(p) preview_dec_ref(*(p))
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_no_clone
#include <stc/hmap.h>

struct load_timer {
  ev_timer watcher;
  Lfm *lfm;
  union {
    Preview *preview;
    Dir *dir;
  };
};

#define i_declared
#define i_type list_load_timer, struct load_timer
#include <stc/dlist.h>

#define i_TYPE set_dir, Dir *
#include <stc/hset.h>

#define i_TYPE set_preview, Preview *
#include <stc/hset.h>

static inline void apply_dir_settings(Dir *dir);

void loader_ctx_init(struct loader_ctx *ctx) {
  ctx->dir_cache = map_zsview_dir_init();
  ctx->preview_cache = map_zsview_preview_init();
}

void loader_ctx_deinit(struct loader_ctx *ctx) {
  list_load_timer_drop(&ctx->dir_timers);
  list_load_timer_drop(&ctx->preview_timers);
  map_zsview_dir_drop(&ctx->dir_cache);
  map_zsview_preview_drop(&ctx->preview_cache);
}

static void dir_load_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  struct load_timer *timer = (struct load_timer *)w;
  Lfm *lfm = timer->lfm;
  async_dir_load(&lfm->async, timer->dir, true);
  timer->dir->is_loading = true;
  ev_timer_stop(EV_A_ w);
  list_load_timer_erase_node(&lfm->loader.dir_timers,
                             list_load_timer_get_node(timer));
}

static void preview_load_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  struct load_timer *timer = (struct load_timer *)w;
  Lfm *lfm = timer->lfm;
  async_preview_load(&lfm->async, timer->preview);
  timer->preview->is_loading = true;
  ev_timer_stop(EV_A_ w);
  list_load_timer_erase_node(&lfm->loader.preview_timers,
                             list_load_timer_get_node(timer));
}

static inline void schedule_dir_load(struct loader_ctx *ctx, Dir *dir,
                                     u64 time) {
  Lfm *lfm = to_lfm(ctx);
  struct load_timer *timer =
      list_load_timer_push(&ctx->dir_timers, (struct load_timer){
                                                 .dir = dir,
                                                 .lfm = lfm,
                                             });
  ev_timer_init(&timer->watcher, dir_load_timer_cb, 0,
                (time - current_millis()) / 1000.);
  ev_timer_again(event_loop, &timer->watcher);
  dir->loadable.next_scheduled = time;
  dir->loadable.next_requested = 0;
  dir->loadable.is_scheduled = true;
}

static inline void schedule_preview_load(struct loader_ctx *ctx, Preview *pv,
                                         u64 time) {
  Lfm *lfm = to_lfm(ctx);
  struct load_timer *timer =
      list_load_timer_push(&ctx->preview_timers, (struct load_timer){
                                                     .preview = pv,
                                                     .lfm = lfm,
                                                 });
  ev_timer_init(&timer->watcher, preview_load_timer_cb, 0,
                (time - current_millis()) / 1000.);
  ev_timer_again(event_loop, &timer->watcher);
  pv->loadable.next_scheduled = time;
  pv->loadable.next_requested = 0;
  pv->loadable.is_scheduled = true;
}

void loader_dir_reload(struct loader_ctx *ctx, Dir *dir) {
  if (unlikely(dir->status == DIR_DISOWNED))
    return;

  if (dir->loadable.is_scheduled)
    return;

  u64 now = current_millis();
  u64 latest = dir->loadable.next_scheduled;

  // Never schedule the same directory more than once. Once the update
  // of the directory is applied we will check if we need to load again.
  if (latest >= now + cfg.inotify_timeout)
    return; // discard

  // Add a (small) delay so we don't show files that exist only very briefly
  u64 next = now < latest + cfg.inotify_timeout
                 ? latest + cfg.inotify_timeout + cfg.inotify_delay
                 : now + cfg.inotify_delay;
  if (dir->is_loading) {
    dir->loadable.next_requested = next;
  } else {
    schedule_dir_load(ctx, dir, next);
  }
}

void loader_dir_load_callback(struct loader_ctx *ctx, Dir *dir) {
  dir->loadable.is_scheduled = false;
  if (dir->loadable.next_requested > 0) {
    u64 now = current_millis();
    if (dir->loadable.next_requested <= now) {
      async_dir_load(&to_lfm(ctx)->async, dir, true);
      dir->loadable.next_scheduled = now;
      dir->loadable.next_requested = 0;
      dir->is_loading = true;
    } else {
      schedule_dir_load(ctx, dir, dir->loadable.next_requested);
    }
  }
}

void loader_preview_reload(struct loader_ctx *ctx, Preview *pv) {
  if (unlikely(pv->status == PV_DISOWNED))
    return;

  if (pv->loadable.is_scheduled)
    return;

  u64 now = current_millis();
  u64 latest = pv->loadable.next_scheduled; // possibly in the future

  if (latest >= now + cfg.inotify_timeout)
    return; // discard

  // Add a small delay so we don't show files that exist only very briefly
  u64 next = now < latest + cfg.inotify_timeout
                 ? latest + cfg.inotify_timeout + cfg.inotify_delay
                 : now + cfg.inotify_delay;

  if (pv->is_loading) {
    pv->loadable.next_requested = next;
  } else {
    schedule_preview_load(ctx, pv, next);
  }
}

void loader_preview_load_callback(struct loader_ctx *ctx, Preview *pv) {
  pv->loadable.is_scheduled = false;
  if (pv->loadable.next_requested > 0) {
    u64 now = current_millis();
    if (pv->loadable.next_requested <= now) {
      async_preview_load(&to_lfm(ctx)->async, pv);
      pv->loadable.next_scheduled = now;
      pv->loadable.next_requested = 0;
      pv->is_loading = true;
    } else {
      schedule_preview_load(ctx, pv, pv->loadable.next_requested);
    }
  }
}

// path must be absolute
Dir *loader_dir_from_path(struct loader_ctx *ctx, zsview path, bool do_load) {
  char buf[PATH_MAX + 1];

  // make sure there is no trailing / in path
  // truncates, in the unlikely case we got passed something longer than
  // PATH_MAX
  if (path.size > 1 && path.str[path.size - 1] == '/') {
    if (path.size + 1 > (ptrdiff_t)sizeof buf)
      path.size = sizeof buf - 1;

    // copy into into our buffer and remove trailing slash
    path.size--;
    memcpy(buf, path.str, path.size);
    buf[path.size] = 0;
    path.str = buf;
  }

  map_zsview_dir_value *v = map_zsview_dir_get_mut(&ctx->dir_cache, path);
  Dir *dir = v ? v->second : NULL;
  if (dir) {
    if (do_load) {
      if (dir->status == DIR_DELAYED) {
        // delayed loading
        async_dir_load(&to_lfm(ctx)->async, dir, false);
        dir->last_loading_action = current_millis();
        ui_start_loading_indicator_timer(&to_lfm(ctx)->ui);
      } else if (dir->status == DIR_LOADED) {
        // don't check before we have actually loaded the directory
        // (in particular stat data which we compare)
        async_dir_check(&to_lfm(ctx)->async, dir);
      }
    }
    dir_set_hidden(dir, cfg.dir_settings.hidden);
  } else {
    dir = dir_create(path, to_lfm(ctx)->fm.height, cfg.scrolloff);
    apply_dir_settings(dir);

    map_zsview_dir_insert(&ctx->dir_cache, dir_path(dir), dir_inc_ref(dir));
    if (do_load) {
      async_dir_load(&to_lfm(ctx)->async, dir, false);
      dir->last_loading_action = current_millis();
      ui_start_loading_indicator_timer(&to_lfm(ctx)->ui);
      dir->is_loading = true;
    }
    if (likely(to_lfm(ctx)->L))
      LFM_RUN_HOOK(to_lfm(ctx), LFM_HOOK_DIRLOADED, path);
  }
  return dir;
}

Preview *loader_preview_from_path(struct loader_ctx *ctx, zsview path,
                                  bool do_load) {
  char fullpath[PATH_MAX + 1];
  if (path_is_relative(path.str)) {
    i32 len = path_make_absolute(path, fullpath, sizeof fullpath);
    path.str = fullpath;
    path.size = len;
  }

  map_zsview_preview_value *v =
      map_zsview_preview_get_mut(&ctx->preview_cache, path);
  Preview *preview;
  if (v) {
    // preview existing in cache
    preview = v->second;

    if (do_load) {
      if (preview->status == PV_DELAYED) {
        async_preview_load(&to_lfm(ctx)->async, preview);
        return preview;
      }
      if (preview->status == PV_LOADED) {
        if (preview->height < to_lfm(ctx)->ui.preview.y ||
            preview->width < to_lfm(ctx)->ui.preview.x) {
          async_preview_load(&to_lfm(ctx)->async, preview);
        } else {
          async_preview_check(&to_lfm(ctx)->async, preview);
        }
      }
    }
  } else {
    preview =
        preview_create_loading(path, to_lfm(ctx)->ui.y, to_lfm(ctx)->ui.x);
    preview_inc_ref(preview);
    map_zsview_preview_insert(&ctx->preview_cache, cstr_zv(&preview->path),
                              preview);
    if (do_load)
      async_preview_load(&to_lfm(ctx)->async, preview);
  }
  return preview;
}

void loader_drop_preview_cache(struct loader_ctx *ctx) {
  c_foreach(it, map_zsview_preview, ctx->preview_cache) {
    (*it.ref).second->status = PV_DISOWNED;
  }
  map_zsview_preview_clear(&ctx->preview_cache);
  c_foreach(it, list_load_timer, ctx->preview_timers) {
    ev_timer_stop(event_loop, &it.ref->watcher);
  }
  list_load_timer_clear(&ctx->preview_timers);
}

void loader_drop_dir_cache(struct loader_ctx *ctx) {
  // we can't drop dirs that are referenced by lua
  // unload those instead and re-insert afterwards
  c_foreach(it, map_zsview_dir, ctx->dir_cache) {
    (*it.ref).second->status = DIR_DISOWNED;
  }
  map_zsview_dir_clear(&ctx->dir_cache);

  c_foreach(it, list_load_timer, ctx->dir_timers) {
    ev_timer_stop(event_loop, &it.ref->watcher);
  }
  list_load_timer_clear(&ctx->dir_timers);
}

void loader_reschedule(struct loader_ctx *ctx) {
  set_dir dirs = set_dir_init();
  c_foreach(it, list_load_timer, ctx->dir_timers) {
    set_dir_insert(&dirs, it.ref->dir);
    ev_timer_stop(event_loop, &it.ref->watcher);
  }
  list_load_timer_clear(&ctx->dir_timers);

  set_preview previews = set_preview_init();
  c_foreach(it, list_load_timer, ctx->preview_timers) {
    set_preview_insert(&previews, it.ref->preview);
    ev_timer_stop(event_loop, &it.ref->watcher);
  }
  list_load_timer_clear(&ctx->preview_timers);

  u64 next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

  c_foreach(it, set_dir, dirs) {
    schedule_dir_load(ctx, *it.ref, next);
  }
  c_foreach(it, set_preview, previews) {
    schedule_preview_load(ctx, *it.ref, next);
  }

  set_dir_drop(&dirs);
  set_preview_drop(&previews);
}

static inline void apply_dir_settings(Dir *dir) {
  hmap_dirsetting_value *v =
      hmap_dirsetting_get_mut(&cfg.dir_settings_map, dir_path(dir));
  struct dir_settings *s = v ? &v->second : &cfg.dir_settings;
  memcpy(&dir->settings, s, sizeof *s);
}
