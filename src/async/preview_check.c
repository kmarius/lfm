#include "private.h"

#include "defs.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "preview.h"

#include <ev.h>
#include <stc/cstr.h>

// TODO: maybe we don't need some of these members since we can now acces the
// preview from the other thread
struct preview_check_work {
  struct result super;
  struct async_ctx *async;
  Preview *preview; // ref-counted
  int height;
  int width;
  time_t mtime;
  u64 load_time;
};

static void destroy(void *p) {
  struct preview_check_work *res = p;
  preview_dec_ref(res->preview);
  xfree(res);
}

static void callback(void *p, Lfm *lfm) {
  struct preview_check_work *res = p;
  loader_preview_reload(&lfm->loader, res->preview);
}

static void worker(void *arg) {
  struct preview_check_work *work = arg;

  /* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
  struct stat statbuf;
  if (stat(preview_path(work->preview).str, &statbuf) == -1 ||
      (statbuf.st_mtime <= work->mtime &&
       statbuf.st_mtime <= (long)(work->load_time / 1000 - 1))) {
    destroy(work);
    return;
  }

  submit_async_result(work->async, (struct result *)work);
}

void async_preview_check(struct async_ctx *async, Preview *pv) {
  struct preview_check_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  work->async = async;
  work->preview = preview_inc_ref(pv);
  work->height = pv->height;
  work->width = pv->width;
  work->mtime = pv->mtime;
  work->load_time = pv->loadtime;

  log_trace("checking preview %s", preview_path_str(pv));
  tpool_add_work(async->tpool, worker, work, true);
}
