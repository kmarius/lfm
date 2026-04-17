#include "private.h"

#include "defs.h"
#include "dir.h"
#include "inotify.h"
#include "lfm.h"
#include "log.h"
#include "memory.h"

#include <ev.h>
#include <stc/cstr.h>

#include <fcntl.h>
#include <unistd.h>

struct inotify_work {
  struct result super;
  struct async_ctx *async;
  Dir *dir; // ref-counted
  bool ok;
};

static void destroy(void *p) {
  struct inotify_work *work = p;
  dir_dec_ref(work->dir);
  xfree(work);
}

static void callback(void *p, Lfm *lfm) {
  struct inotify_work *work = p;
  set_result_erase(&lfm->async.in_progress.inotify, p);
  if (likely(work->ok))
    inotify_add_watcher(&lfm->inotify, work->dir);
  if (lfm->async.in_progress.inotify_preview == p)
    lfm->async.in_progress.inotify_preview = NULL;
}

static void worker(void *arg) {
  struct inotify_work *work = arg;

  // We open the directory here so that the inotify watcher
  // can be added immediately. Otherwise, the call to inotify_add_watch
  // can block for several seconds e.g. on automounted nfs mounts.
  int fd = open(dir_path(work->dir).str, O_RDONLY);
  if (likely(fd > 0)) {
    work->ok = true;
    close(fd);
  }

  submit_async_result(work->async, (struct result *)work);
}

void async_inotify_add(struct async_ctx *async, Dir *dir) {
  struct inotify_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  work->async = async;
  work->dir = dir_inc_ref(dir);

  log_trace("adding inotify watcher %s", dir_path_str(dir));
  set_result_insert(&async->in_progress.inotify, &work->super);
  tpool_add_work(async->tpool, worker, work, true);
}

void async_inotify_add_previewed(struct async_ctx *async, Dir *dir) {
  struct inotify_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  work->async = async;
  work->dir = dir_inc_ref(dir);

  cancel(async->in_progress.inotify_preview);
  async->in_progress.inotify_preview = &work->super;

  log_trace("adding inotify watcher %s", dir_path_str(dir));
  tpool_add_work(async->tpool, worker, work, true);
}

void async_inotify_cancel(struct async_ctx *async) {
  (void)async;
  c_foreach(it, set_result, async->in_progress.inotify) {
    cancel(*it.ref);
  }
  set_result_clear(&async->in_progress.inotify);
  cancel(async->in_progress.inotify_preview);
  async->in_progress.inotify_preview = NULL;
}
