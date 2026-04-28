#include "loader.h"
#include "async/async.h"
#include "config.h"
#include "defs.h"
#include "dir.h"
#include "hooks.h"
#include "lfm.h"
#include "loadable.h"
#include "loop.h"
#include "memory.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <stc/common.h>
#include <stc/cstr.h>
#include <stc/zsview.h>

#include <stddef.h>

// key is zsview of preview->path and owned by preview
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
  struct loader_ctx *loader;
  struct loadable_data *loadable;
};

#define i_declared
#define i_type map_loadable_timer, struct loadable_data *, struct load_timer *
#define i_valdrop(p) (ev_timer_stop(event_loop, &(*p)->watcher), xfree(*p))
#define i_no_clone
#include <stc/hmap.h>

static void load_timer_cb(EV_P_ ev_timer *w, i32 revents);
static inline void apply_dir_settings(Dir *dir);

void loader_ctx_init(struct loader_ctx *ctx) {
  memset(ctx, 0, sizeof *ctx);
}

void loader_ctx_deinit(struct loader_ctx *ctx) {
  map_loadable_timer_drop(&ctx->timers);
  map_zsview_dir_drop(&ctx->dir_cache);
  map_zsview_preview_drop(&ctx->preview_cache);
}

static inline void load(struct loader_ctx *ctx,
                        struct loadable_data *loadable) {
  loadable->load(ctx, loadable);
  loadable->next_scheduled = current_millis();
  loadable->next_requested = 0;
  loadable->in_progress = true;
}

// Schedule the existing timer for the loadable
static inline void reschedule(struct loadable_data *loadable, ev_timer *w,
                              u64 time_ms, f64 delay) {
  ev_timer_set(w, 0, delay);
  ev_timer_again(event_loop, w);
  loadable->next_scheduled = time_ms;
  loadable->next_requested = 0;
  loadable->is_scheduled = true;
}

// Schedule a loadable at the given time in milliseconds.
// Creates a new load_timer struct, adds it to the map and
// schedules the timer.
static inline void schedule(struct loader_ctx *ctx,
                            struct loadable_data *loadable, u64 time_ms) {
  if (loadable->is_scheduled)
    return;

  assert(!map_loadable_timer_contains(&ctx->timers, loadable));

  struct load_timer *timer = xcalloc(1, sizeof *timer);
  *timer = (struct load_timer){
      .loadable = loadable,
      .loader = ctx,
  };
  map_loadable_timer_insert(&ctx->timers, loadable, timer);
  ev_timer_init(&timer->watcher, load_timer_cb, 0, 0);

  f64 delay = (time_ms - current_millis()) / 1000.;
  reschedule(loadable, &timer->watcher, time_ms, delay);
}

static void load_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  // timer stopped in erase

  struct load_timer *timer = (struct load_timer *)w;
  struct loader_ctx *loader = timer->loader;
  timer->loadable->is_scheduled = false;
  load(loader, timer->loadable);
  map_loadable_timer_erase(&loader->timers, timer->loadable);
}

void loader_callback(struct loader_ctx *ctx, struct loadable_data *loadable) {
  loadable->in_progress = false;
  if (loadable->next_requested > 0) {
    // another reload was requested while we were loading
    // load immediately or schedule a reload
    u64 now = current_millis();
    if (loadable->next_requested <= now) {
      load(ctx, loadable);
    } else {
      schedule(ctx, loadable, loadable->next_requested);
    }
  }
}

void loader_reload(struct loader_ctx *ctx, struct loadable_data *loadable) {
  if (loadable->is_scheduled || loadable->next_requested)
    return; // already in progress or an active timer

  u64 now = current_millis();
  u64 latest = loadable->next_scheduled;

  // Never schedule the same directory more than once. Once the update
  // of the directory is applied we will check if we need to load again.
  if (latest >= now + cfg.inotify_timeout)
    return; // discard

  // Add a (small) delay so we don't show files that exist only very briefly
  u64 next = now < latest + cfg.inotify_timeout
                 ? latest + cfg.inotify_timeout + cfg.inotify_delay
                 : now + cfg.inotify_delay;

  if (loadable->in_progress) {
    // loading in progress, we won't schedule here, instead
    // we do it in the callback when it finishes
    loadable->next_requested = next;
  } else {
    schedule(ctx, loadable, next);
  }
}

void loader_reschedule(struct loader_ctx *ctx) {
  u64 next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;
  f64 delay = (cfg.inotify_timeout + cfg.inotify_delay) / 1000.;
  c_foreach(it, map_loadable_timer, ctx->timers) {
    ev_timer_stop(event_loop, &it.ref->second->watcher);
    reschedule((*it.ref).first, &(*it.ref).second->watcher, next, delay);
  }
}

//
// Dir/Preview specific stuff
//

static void load_dir(struct loader_ctx *ctx, struct loadable_data *loadable) {
  Dir *dir = container_of(loadable, Dir, loadable);
  bool load_fileinfo = dir->status != DIR_DELAYED;
  async_dir_load(&to_lfm(ctx)->async, dir, load_fileinfo);
  dir->last_loading_action = current_millis();
  ui_start_loading_indicator_timer(&to_lfm(ctx)->ui);
}

static void load_preview(struct loader_ctx *ctx,
                         struct loadable_data *loadable) {
  Preview *dir = container_of(loadable, Preview, loadable);
  async_preview_load(&to_lfm(ctx)->async, dir);
}

void loader_dir_reload(struct loader_ctx *ctx, Dir *dir) {
  if (dir->loadable.is_disowned || dir->loadable.is_scheduled ||
      dir->loadable.next_requested)
    return;
  loader_reload(ctx, &dir->loadable);
}

void loader_preview_reload(struct loader_ctx *ctx, Preview *pv) {
  if (pv->loadable.is_disowned || pv->loadable.is_scheduled ||
      pv->loadable.next_requested)
    return;
  pv->loadable.load = load_preview;
  loader_reload(ctx, &pv->loadable);
}

// make sure there is no trailing / in path
// truncates, in the unlikely case we got passed something longer than
// PATH_MAX
static inline zsview remove_trailing_slash(zsview path, char *buf,
                                           usize bufsz) {
  // make sure not completely erase / for the root
  if (unlikely(path.size > 1 && path.str[path.size - 1] == '/')) {
    if (path.size + 1 > (ptrdiff_t)bufsz)
      path.size = sizeof buf - 1;
    path.size--;
    memcpy(buf, path.str, path.size);
    buf[path.size] = 0;
    path.str = buf;
  }
  return path;
}

// path must be absolute
Dir *loader_dir_from_path(struct loader_ctx *ctx, zsview path, bool do_load) {
  char buf[PATH_MAX + 1];
  path = remove_trailing_slash(path, buf, sizeof buf);

  map_zsview_dir_value *v = map_zsview_dir_get_mut(&ctx->dir_cache, path);
  Dir *dir = v ? v->second : NULL;
  if (dir) {
    if (do_load) {
      if (dir->status == DIR_DELAYED) {
        // delayed loading
        load_dir(ctx, &dir->loadable);
      } else if (dir->status == DIR_LOADED) {
        async_dir_check(&to_lfm(ctx)->async, dir);
      }
    }
    dir_set_hidden(dir, cfg.dir_settings.hidden);
  } else {
    dir = dir_create(path, to_lfm(ctx)->fm.height, cfg.scrolloff);
    dir->loadable.load = load_dir;
    apply_dir_settings(dir);

    map_zsview_dir_insert(&ctx->dir_cache, dir_path(dir), dir_inc_ref(dir));
    if (do_load) {
      load_dir(ctx, &dir->loadable);
      dir->is_loading = true;
    }
    if (likely(to_lfm(ctx)->L))
      LFM_RUN_HOOK(to_lfm(ctx), LFM_HOOK_DIRLOADED, path);
  }
  return dir;
}

Preview *loader_preview_from_path(struct loader_ctx *ctx, zsview path,
                                  bool do_load) {
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
    Preview *pv = (*it.ref).second;
    pv->loadable.is_disowned = true;
    map_loadable_timer_erase(&ctx->timers, &pv->loadable);
  }
  map_zsview_preview_clear(&ctx->preview_cache);
}

void loader_drop_dir_cache(struct loader_ctx *ctx) {
  c_foreach(it, map_zsview_dir, ctx->dir_cache) {
    Dir *dir = (*it.ref).second;
    dir->loadable.is_disowned = true;
    map_loadable_timer_erase(&ctx->timers, &dir->loadable);
  }
  map_zsview_dir_clear(&ctx->dir_cache);
}

static inline void apply_dir_settings(Dir *dir) {
  hmap_dirsetting_value *v =
      hmap_dirsetting_get_mut(&cfg.dir_settings_map, dir_path(dir));
  struct dir_settings *s = v ? &v->second : &cfg.dir_settings;
  memcpy(&dir->settings, s, sizeof *s);
}
