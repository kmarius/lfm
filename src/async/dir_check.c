#include "private.h"

#include "defs.h"
#include "dir.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "ui.h"
#include "util.h"

struct dir_check_work {
  struct result super;
  struct async_ctx *async;
  Dir *dir;
  time_t mtime;
  __ino_t ino;
  bool reload; // should we reload
};

static void destroy(void *p) {
  struct dir_check_work *work = p;
  dir_dec_ref(work->dir);
  xfree(work);
}

static void callback(void *p, Lfm *lfm) {
  struct dir_check_work *work = p;
  if (work->reload) {
    loader_dir_reload(&lfm->loader, work->dir);
  } else {
    work->dir->last_loading_action = 0;
  }
  set_result_erase(&lfm->async.in_progress.dirs, &work->super);
}

static void worker(void *arg) {
  struct dir_check_work *work = arg;

  struct stat statbuf;
  if (stat(dir_path(work->dir).str, &statbuf) == -1 ||
      (statbuf.st_ino == work->ino && statbuf.st_mtime <= work->mtime)) {
    work->reload = false;
  } else {
    work->reload = true;
  }

  submit_async_result(work->async, (struct result *)work);
}

void async_dir_check(struct async_ctx *async, Dir *dir) {
  struct dir_check_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  if (dir->last_loading_action == 0) {
    dir->last_loading_action = current_millis();
    ui_start_loading_indicator_timer(&to_lfm(async)->ui);
  }

  work->async = async;
  work->dir = dir_inc_ref(dir);
  work->mtime = dir->stat.st_mtime;
  work->ino = dir->stat.st_ino;

  set_result_insert(&async->in_progress.dirs, &work->super);

  log_trace("checking directory %s", dir_path_str(dir));
  tpool_add_work(async->tpool, worker, work, true);
}
