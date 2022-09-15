#include <ev.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "hashtab.h"
#include "loader.h"
#include "log.h"
#include "util.h"

static Lfm *g_lfm = NULL;
static struct ev_loop *g_loop = NULL;

static ev_timer **g_dir_timers = NULL;
Hashtab *g_dir_tab = NULL;

static ev_timer **g_pv_timers = NULL;
Hashtab *g_pv_tab = NULL;


void loader_init(void *_lfm)
{
  g_lfm = _lfm;
  g_loop = g_lfm->loop;
  g_dir_tab = ht_create((free_fun) dir_destroy);
  g_pv_tab = ht_create((free_fun) preview_destroy);
}


void loader_deinit()
{
  cvector_ffree(g_dir_timers, free);
  cvector_ffree(g_pv_timers, free);
  ht_destroy(g_dir_tab);
  ht_destroy(g_pv_tab);
}


static void dir_timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  async_dir_load(w->data, true);
  ev_timer_stop(loop, w);
  free(w);
  cvector_swap_remove(g_dir_timers, w);
}


static void pv_timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  async_preview_load(w->data, g_lfm->ui.nrow);
  ev_timer_stop(loop, w);
  free(w);
  cvector_swap_remove(g_pv_timers, w);
}


static inline void schedule_dir_load(Dir *dir, uint64_t time)
{
  log_debug("schedule_dir_load %s", dir->path);
  ev_timer *timer = malloc(sizeof *timer);
  ev_timer_init(timer, dir_timer_cb, 0, (time - current_millis()) / 1000.);
  timer->data = dir;
  ev_timer_again(g_loop, timer);
  cvector_push_back(g_dir_timers, timer);
}


static inline void schedule_preview_load(Preview *pv, uint64_t time)
{
  ev_timer *timer = malloc(sizeof *timer);
  ev_timer_init(timer, pv_timer_cb, 0, (time - current_millis()) / 1000.);
  timer->data = pv;
  ev_timer_again(g_loop, timer);
  cvector_push_back(g_pv_timers, timer);
}


void loader_dir_reload(Dir *dir)
{
  uint64_t now = current_millis();
  uint64_t latest = dir->next;  // possibly in the future

  if (latest >= now + cfg.inotify_timeout) {
    return;  // discard
  }

  // Add a small delay so we don't show files that exist only very briefly
  uint64_t next = now < latest + cfg.inotify_timeout
    ? latest + cfg.inotify_timeout + cfg.inotify_delay
    : now + cfg.inotify_delay;
  schedule_dir_load(dir, next);
  dir->next = next;
}


void loader_preview_reload(Preview *pv)
{
  uint64_t now = current_millis();
  uint64_t latest = pv->next;  // possibly in the future

  if (latest >= now + cfg.inotify_timeout) {
    return;  // discard
  }

  // Add a small delay so we don't show files that exist only very briefly
  uint64_t next = now < latest + cfg.inotify_timeout
    ? latest + cfg.inotify_timeout + cfg.inotify_delay
    : now + cfg.inotify_delay;
  schedule_preview_load(pv, next);
  pv->next = next;
}


Dir *loader_dir_from_path(const char *path)
{
  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  Dir *dir = ht_get(g_dir_tab, path);
  if (dir) {
    async_dir_check(dir);
    dir->hidden = cfg.hidden;
    dir_sort(dir);
  } else {
    /* At this point, we should not print this new directory, but
     * start a timer for, say, 250ms. When the timer runs out we draw the
     * "loading" directory regardless. The timer should be cancelled when:
     * 1. the actual directory arrives after loading from disk
     * 2. we navigate to a different directory (possibly restart a timer there)
     *
     * Check how this behaves in the preview pane when just scrolling over
     * directories.
     */
    dir = dir_create(path);
    dir->hidden = cfg.hidden;
    ht_set(g_dir_tab, dir->path, dir);
    async_dir_load(dir, false);
  }
  return dir;
}


Preview *loader_preview_from_path(const char *path, bool image)
{
  char fullpath[PATH_MAX];
  if (path_is_relative(path)) {
    snprintf(fullpath, sizeof fullpath, "%s/%s", getenv("PWD"), path);
    path = fullpath;
  }

  Preview *pv = ht_get(g_pv_tab, path);
  if (pv) {
    if (pv->nrow < g_lfm->ui.nrow) {
      /* TODO: don't need to reload if the actual file holds fewer lines (on 2022-09-14) */
      async_preview_load(pv, g_lfm->ui.nrow);
    } else {
      async_preview_check(pv);
    }
  } else {
    pv = preview_create_loading(path, g_lfm->ui.nrow, image);
    ht_set(g_pv_tab, pv->path, pv);
    async_preview_load(pv, g_lfm->ui.nrow);
  }
  return pv;
}


Hashtab *loader_dir_hashtab()
{
  return g_dir_tab;
}


Hashtab *loader_pv_hashtab()
{
  return g_pv_tab;
}


void loader_drop_preview_cache()
{
  ht_clear(g_pv_tab);
  cvector_foreach(ev_timer *timer, g_pv_timers) {
    ev_timer_stop(g_loop, timer);
    free(timer);
  }
  cvector_set_size(g_pv_timers, 0);
}


void loader_drop_dir_cache()
{
  ht_clear(g_dir_tab);
  cvector_foreach(ev_timer *timer, g_dir_timers) {
    ev_timer_stop(g_loop, timer);
    free(timer);
  }
  cvector_set_size(g_dir_timers, 0);
}


void loader_reschedule()
{
  Dir **dirs = NULL;
  cvector_foreach(ev_timer *timer, g_dir_timers) {
    if (!cvector_contains(dirs, timer->data)) {
      cvector_push_back(dirs, timer->data);
    }
    ev_timer_stop(g_loop, timer);
    free(timer);
  }
  cvector_set_size(g_dir_timers, 0);

  Preview **previews = NULL;
  cvector_foreach(ev_timer *timer, g_pv_timers) {
    if (!cvector_contains(previews, timer->data)) {
      cvector_push_back(previews, timer->data);
    }
    ev_timer_stop(g_loop, timer);
    free(timer);
  }
  cvector_set_size(g_pv_timers, 0);

  uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

  cvector_foreach(Dir *dir, dirs) {
    schedule_dir_load(dir, next);
  }
  cvector_foreach(Preview *pv, previews) {
    schedule_preview_load(pv, next);
  }
  cvector_free(dirs);
  cvector_free(previews);
}
