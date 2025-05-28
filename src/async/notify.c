#include "private.h"

#include "../dir.h"
#include "../fm.h"
#include "../lfm.h"
#include "../loader.h"
#include "../log.h"
#include "../macros.h"
#include "../memory.h"
#include "../notify.h"
#include "../stc/cstr.h"

#include <ev.h>

#include <dirent.h>
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
  struct validity_check64 check0;
  struct validity_check64 check1;
};

static void notify_add_result_destroy(void *p) {
  struct notify_add_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void notify_add_result_callback(void *p, Lfm *lfm) {
  struct notify_add_data *res = p;
  if (CHECK_PASSES(res->check0) && CHECK_PASSES(res->check1)) {
    notify_add_watcher(&lfm->notify, res->dir);
  }
  notify_add_result_destroy(p);
}

void async_notify_add_worker(void *arg) {
  struct notify_add_data *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    notify_add_result_destroy(work);
    return;
  }
  // We open the directory here so that adding the notify watcher
  // can be added immediately. Otherwise, the call to inotify_add_watch
  // can block for several seconds e.g. on automounted nfs mounts.
  DIR *dirp = opendir(work->path);
  if (!dirp) {
    notify_add_result_destroy(work);
    return;
  }
  closedir(dirp);

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_notify_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = cstr_strdup(dir_path(dir));
  work->dir = dir;
  CHECK_INIT(work->check0, to_lfm(async)->notify.version);
  CHECK_INIT(work->check1, to_lfm(async)->loader.dir_cache_version);

  log_trace("watching %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}

void async_notify_preview_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = cstr_strdup(dir_path(dir));
  work->dir = dir;
  CHECK_INIT(work->check0, to_lfm(async)->notify.version);
  CHECK_INIT(work->check1, to_lfm(async)->fm.dirs.preview);

  log_trace("watching %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}
