#include "private.h"

#include "defs.h"
#include "dir.h"
#include "lfm.h"
#include "memory.h"
#include "notify.h"
#include "stc/cstr.h"

#include <ev.h>

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

struct notify_add_data {
  struct result super;
  Async *async;
  char *path;
  Dir *dir;
  bool ok;
};

static void notify_add_result_destroy(void *p) {
  struct notify_add_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void notify_add_result_callback(void *p, Lfm *lfm) {
  struct notify_add_data *res = p;
  if (res->ok)
    notify_add_watcher(&lfm->notify, res->dir);
  set_result_erase(&lfm->async.in_progress.notify, p);
  if (lfm->async.in_progress.notify_preview == p)
    lfm->async.in_progress.notify_preview = NULL;
}

void async_notify_add_worker(void *arg) {
  struct notify_add_data *work = arg;

  // We open the directory here so that the notify watcher
  // can be added immediately. Otherwise, the call to inotify_add_watch
  // can block for several seconds e.g. on automounted nfs mounts.
  int fd = open(work->path, O_RDONLY);
  if (likely(fd > 0)) {
    work->ok = true;
    close(fd);
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_notify_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = zsview_strdup(dir_path(dir));
  work->dir = dir;

  set_result_insert(&async->in_progress.notify, &work->super);
  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}

void async_notify_preview_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = zsview_strdup(dir_path(dir));
  work->dir = dir;

  set_result_insert(&async->in_progress.notify, &work->super);
  if (async->in_progress.notify_preview)
    cancel(async->in_progress.notify_preview);
  async->in_progress.notify_preview = &work->super;

  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}

void async_notify_cancel(Async *async) {
  (void)async;
  c_foreach(it, set_result, async->in_progress.notify) {
    cancel(*it.ref);
  }
  set_result_clear(&async->in_progress.notify);
  async->in_progress.notify_preview = NULL;
}
