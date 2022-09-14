#include <ev.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "hashtab.h"
#include "loader.h"
#include "log.h"
#include "util.h"

static Lfm *lfm = NULL;
static struct ev_loop *loop = NULL;

static ev_timer **dir_timers = NULL;
Hashtab *dir_tab = NULL;

static ev_timer **pv_timers = NULL;
Hashtab *pv_tab = NULL;


void loader_init(void *_lfm)
{
  lfm = _lfm;
  loop = lfm->loop;
  dir_tab = ht_create(LOADER_TAB_SIZE, (free_fun) dir_destroy);
  pv_tab = ht_create(LOADER_TAB_SIZE, (free_fun) preview_destroy);
}


void loader_deinit()
{
  cvector_ffree(dir_timers, free);
  cvector_ffree(pv_timers, free);
  ht_destroy(dir_tab);
  ht_destroy(pv_tab);
}


static void dir_timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  async_dir_load(w->data, true);
  ev_timer_stop(loop, w);
  free(w);
  cvector_swap_remove(dir_timers, w);
}


static void pv_timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  async_preview_load(w->data, lfm->ui.nrow);
  ev_timer_stop(loop, w);
  free(w);
  cvector_swap_remove(pv_timers, w);
}


static inline void schedule_dir_load(Dir *dir, uint64_t time)
{
  log_debug("schedule_dir_load %s", dir->path);
  ev_timer *timer = malloc(sizeof *timer);
  ev_timer_init(timer, dir_timer_cb, 0, (time - current_millis()) / 1000.);
  timer->data = dir;
  ev_timer_again(loop, timer);
  cvector_push_back(dir_timers, timer);
}


static inline void schedule_preview_load(Preview *pv, uint64_t time)
{
  ev_timer *timer = malloc(sizeof *timer);
  ev_timer_init(timer, pv_timer_cb, 0, (time - current_millis()) / 1000.);
  timer->data = pv;
  ev_timer_again(loop, timer);
  cvector_push_back(pv_timers, timer);
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

  Dir *dir = ht_get(dir_tab, path);
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
    ht_set(dir_tab, dir->path, dir);
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

  Preview *pv = ht_get(pv_tab, path);
  if (pv) {
    async_preview_check(pv);
  } else {
    pv = preview_create_loading(path, lfm->ui.nrow, image);
    ht_set(pv_tab, pv->path, pv);
    async_preview_load(pv, lfm->ui.nrow);
  }
  return pv;
}


Hashtab *loader_dir_hashtab()
{
  return dir_tab;
}


Hashtab *loader_pv_hashtab()
{
  return pv_tab;
}


void loader_drop_preview_cache()
{
  ht_clear(pv_tab);
  cvector_foreach(ev_timer *timer, pv_timers) {
    ev_timer_stop(loop, timer);
    free(timer);
  }
  cvector_set_size(pv_timers, 0);
}


void loader_drop_dir_cache()
{
  ht_clear(dir_tab);
  cvector_foreach(ev_timer *timer, dir_timers) {
    ev_timer_stop(loop, timer);
    free(timer);
  }
  cvector_set_size(dir_timers, 0);
}


void loader_reschedule()
{
  Dir **dirs = NULL;
  cvector_foreach(ev_timer *timer, dir_timers) {
    if (!cvector_contains(dirs, timer->data)) {
      cvector_push_back(dirs, timer->data);
    }
    ev_timer_stop(loop, timer);
    free(timer);
  }
  cvector_set_size(dir_timers, 0);

  Preview **previews = NULL;
  cvector_foreach(ev_timer *timer, pv_timers) {
    if (!cvector_contains(previews, timer->data)) {
      cvector_push_back(previews, timer->data);
    }
    ev_timer_stop(loop, timer);
    free(timer);
  }
  cvector_set_size(pv_timers, 0);

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
