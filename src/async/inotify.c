#include "private.h"

#include "defs.h"
#include "dir.h"
#include "inotify.h"
#include "lfm.h"
#include "memory.h"
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

struct inotify_work {
  struct result super;
  Async *async;
  Dir *dir; // ref-counted
  bool ok;
};

static void inotify_result_destroy(void *p) {
  struct inotify_work *res = p;
  dir_dec_ref(res->dir);
  xfree(res);
}

static void inotify_result_callback(void *p, Lfm *lfm) {
  struct inotify_work *res = p;
  set_result_erase(&lfm->async.in_progress.inotify, p);
  if (res->ok)
    inotify_add_watcher(&lfm->inotify, res->dir);
  if (lfm->async.in_progress.inotify_preview == p)
    lfm->async.in_progress.inotify_preview = NULL;
}

static void async_inotify_worker(void *arg) {
  struct inotify_work *work = arg;

  // We open the directory here so that the inotify watcher
  // can be added immediately. Otherwise, the call to inotify_add_watch
  // can block for several seconds e.g. on automounted nfs mounts.
  int fd = open(dir_path(work->dir).str, O_RDONLY);
  if (likely(fd > 0)) {
    work->ok = true;
    close(fd);
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_inotify_add(Async *async, Dir *dir) {
  struct inotify_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &inotify_result_callback;
  work->super.destroy = &inotify_result_destroy;

  work->async = async;
  work->dir = dir_inc_ref(dir);

  set_result_insert(&async->in_progress.inotify, &work->super);
  tpool_add_work(async->tpool, async_inotify_worker, work, true);
}

void async_inotify_add_previewed(Async *async, Dir *dir) {
  struct inotify_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &inotify_result_callback;
  work->super.destroy = &inotify_result_destroy;

  work->async = async;
  work->dir = dir_inc_ref(dir);

  cancel(async->in_progress.inotify_preview);
  async->in_progress.inotify_preview = &work->super;

  tpool_add_work(async->tpool, async_inotify_worker, work, true);
}

void async_inotify_cancel(Async *async) {
  (void)async;
  c_foreach(it, set_result, async->in_progress.inotify) {
    cancel(*it.ref);
  }
  set_result_clear(&async->in_progress.inotify);
  cancel(async->in_progress.inotify_preview);
  async->in_progress.inotify_preview = NULL;
}
