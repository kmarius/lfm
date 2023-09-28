#include <ev.h>
#include <stdint.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "fm.h"
#include "hashtab.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "ui.h"
#include "util.h"

struct timer_data {
  Lfm *lfm;
  union {
    Preview *preview;
    Dir *dir;
  };
};

#define DATA(w) ((struct timer_data *)w->data)

#define get_lfm(loader_) container_of(loader_, struct lfm_s, loader)

void loader_init(Loader *loader) {
  loader->dir_cache = ht_create((ht_free_func)dir_destroy);
  loader->preview_cache = ht_create((ht_free_func)preview_destroy);
}

void loader_deinit(Loader *loader) {
  cvector_foreach(struct ev_timer * timer, loader->dir_timers) {
    xfree(timer->data);
    xfree(timer);
  }
  cvector_foreach(struct ev_timer * timer, loader->preview_timers) {
    xfree(timer->data);
    xfree(timer);
  }
  cvector_free(loader->dir_timers);
  cvector_free(loader->preview_timers);
  ht_destroy(loader->dir_cache);
  ht_destroy(loader->preview_cache);
}

static void dir_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct timer_data *data = w->data;
  async_dir_load(&data->lfm->async, data->dir, true);
  data->dir->loading = true;
  ev_timer_stop(EV_A_ w);
  cvector_swap_remove(data->lfm->loader.dir_timers, w);
  xfree(data);
  xfree(w);
}

static void pv_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct timer_data *data = w->data;
  async_preview_load(&data->lfm->async, data->preview);
  ev_timer_stop(EV_A_ w);
  cvector_swap_remove(data->lfm->loader.preview_timers, w);
  xfree(data);
  xfree(w);
}

static inline void schedule_dir_load(Loader *loader, Dir *dir, uint64_t time) {
  ev_timer *timer = xmalloc(sizeof *timer);
  double delay = (time - current_millis()) / 1000.;
  ev_timer_init(timer, dir_timer_cb, 0, delay);
  struct timer_data *data = xmalloc(sizeof *data);
  data->dir = dir;
  data->lfm = get_lfm(loader);
  timer->data = data;
  ev_timer_again(get_lfm(loader)->loop, timer);
  cvector_push_back(loader->dir_timers, timer);
  dir->next_scheduled_load = time;
  dir->next_requested_load = 0;
  dir->scheduled = true;
  log_trace("scheduled %s in %fs", dir->path, delay);
}

static inline void schedule_preview_load(Loader *loader, Preview *pv,
                                         uint64_t time) {
  ev_timer *timer = xmalloc(sizeof *timer);
  ev_timer_init(timer, pv_timer_cb, 0, (time - current_millis()) / 1000.);
  struct timer_data *data = xmalloc(sizeof *data);
  data->preview = pv;
  data->lfm = get_lfm(loader);
  timer->data = data;
  ev_timer_again(get_lfm(loader)->loop, timer);
  cvector_push_back(loader->preview_timers, timer);
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
      async_dir_load(&get_lfm(loader)->async, dir, true);
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

Dir *loader_dir_from_path(Loader *loader, const char *path) {
  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  Dir *dir = ht_get(loader->dir_cache, path);
  if (dir) {
    if (dir->updates > 0) {
      // don't check before we have actually loaded the directory
      // (in particular stat data which we compare)
      async_dir_check(&get_lfm(loader)->async, dir);
    }
    /* TODO: no (on 2022-10-09) */
    dir->settings.hidden = cfg.dir_settings.hidden;
    dir_sort(dir);
  } else {
    dir = dir_create(path);
    struct dir_settings *s = ht_get(cfg.dir_settings_map, path);
    memcpy(&dir->settings, s ? s : &cfg.dir_settings, sizeof *s);
    ht_set(loader->dir_cache, dir->path, dir);
    async_dir_load(&get_lfm(loader)->async, dir, false);
    dir->last_loading_action = current_millis();
    lfm_start_loading_indicator_timer(get_lfm(loader));
    dir->loading = true;
    if (get_lfm(loader)->L) {
      lfm_run_hook1(get_lfm(loader), LFM_HOOK_DIRLOADED, path);
    }
  }
  return dir;
}

Preview *loader_preview_from_path(Loader *loader, const char *path) {
  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  Preview *pv = ht_get(loader->preview_cache, path);
  if (pv) {
    if (pv->reload_height < (int)get_lfm(loader)->ui.preview.rows ||
        pv->reload_width < (int)get_lfm(loader)->ui.preview.cols) {
      /* TODO: don't need to reload text previews if the actual file holds fewer
       * lines (on 2022-09-14) */
      async_preview_load(&get_lfm(loader)->async, pv);
    } else {
      async_preview_check(&get_lfm(loader)->async, pv);
    }
  } else {
    pv = preview_create_loading(path, get_lfm(loader)->ui.nrow,
                                get_lfm(loader)->ui.ncol);
    ht_set(loader->preview_cache, pv->path, pv);
    async_preview_load(&get_lfm(loader)->async, pv);
  }
  return pv;
}

void loader_drop_preview_cache(Loader *loader) {
  loader->preview_cache_version++;
  ht_clear(loader->preview_cache);
  cvector_foreach(ev_timer * timer, loader->preview_timers) {
    ev_timer_stop(get_lfm(loader)->loop, timer);
    xfree(timer);
  }
  cvector_set_size(loader->preview_timers, 0);
}

void loader_drop_dir_cache(Loader *loader) {
  loader->dir_cache_version++;
  ht_clear(loader->dir_cache);
  cvector_foreach(ev_timer * timer, loader->dir_timers) {
    ev_timer_stop(get_lfm(loader)->loop, timer);
    xfree(timer);
  }
  cvector_set_size(loader->dir_timers, 0);
}

void loader_reschedule(Loader *loader) {
  Dir **dirs = NULL;
  bool contained;
  cvector_foreach(ev_timer * timer, loader->dir_timers) {
    cvector_contains(dirs, DATA(timer)->dir, contained);
    if (!contained) {
      cvector_push_back(dirs, DATA(timer)->dir);
    }
    ev_timer_stop(get_lfm(loader)->loop, timer);
    xfree(timer->data);
    xfree(timer);
  }
  cvector_set_size(loader->dir_timers, 0);

  Preview **previews = NULL;
  cvector_foreach(ev_timer * timer, loader->preview_timers) {
    cvector_contains(previews, DATA(timer)->preview, contained);
    if (!contained) {
      cvector_push_back(previews, DATA(timer)->preview);
    }
    ev_timer_stop(get_lfm(loader)->loop, timer);
    xfree(timer->data);
    xfree(timer);
  }
  cvector_set_size(loader->preview_timers, 0);

  uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

  cvector_foreach(Dir * dir, dirs) {
    schedule_dir_load(loader, dir, next);
  }
  cvector_foreach(Preview * pv, previews) {
    schedule_preview_load(loader, pv, next);
  }
  cvector_free(dirs);
  cvector_free(previews);
}
