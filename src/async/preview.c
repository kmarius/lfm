#include "private.h"

#include "../config.h"
#include "../lfm.h"
#include "../loader.h"
#include "../log.h"
#include "../macros.h"
#include "../memory.h"
#include "../preview.h"
#include "../stc/cstr.h"
#include "../ui.h"

#include <ev.h>

#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

struct preview_check_data {
  struct result super;
  Async *async;
  char *path;
  int height;
  int width;
  time_t mtime;
  uint64_t loadtime;
};

struct preview_load_data {
  struct result super;
  Async *async;
  Preview *preview; // not guaranteed to exist, do not touch
  char *path;
  int width;
  int height;
  Preview *update;
  struct validity_check64 check;
};

static void preview_check_destroy(void *p) {
  struct preview_check_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void preview_check_callback(void *p, Lfm *lfm) {
  struct preview_check_data *res = p;
  Preview *pv = loader_preview_get(&lfm->loader, zsview_from(res->path));
  if (pv) {
    loader_preview_reload(&lfm->loader, pv);
  }
  preview_check_destroy(p);
}

static void async_preview_check_worker(void *arg) {
  struct preview_check_data *work = arg;
  struct stat statbuf;

  /* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
  if (stat(work->path, &statbuf) == -1 ||
      (statbuf.st_mtime <= work->mtime &&
       statbuf.st_mtime <= (long)(work->loadtime / 1000 - 1))) {
    preview_check_destroy(work);
    return;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_preview_check(Async *async, Preview *pv) {
  struct preview_check_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &preview_check_callback;
  work->super.destroy = &preview_check_destroy;
  work->super.next = NULL;

  work->async = async;
  work->path = cstr_strdup(preview_path(pv));
  work->height = pv->reload_height;
  work->width = pv->reload_width;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;

  log_trace("checking preview %s", preview_path_str(pv));
  tpool_add_work(async->tpool, async_preview_check_worker, work, true);
}

static void preview_load_destroy(void *p) {
  struct preview_load_data *res = p;
  preview_destroy(res->update);
  xfree(res->path);
  xfree(res);
}

static void preview_load_callback(void *p, Lfm *lfm) {
  struct preview_load_data *res = p;
  if (CHECK_PASSES(res->check)) {
    preview_update(res->preview, res->update);
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
    res->update = NULL;
  }
  preview_load_destroy(p);
}

static void async_preview_load_worker(void *arg) {
  struct preview_load_data *work = arg;

  work->update = preview_create_from_file(zsview_from(work->path), work->width,
                                          work->height);
  enqueue_and_signal(work->async, (struct result *)work);
}

void async_preview_load(Async *async, Preview *pv) {
  if (bytes_is_empty(cfg.lua_previewer)) {

    struct preview_load_data *work = xcalloc(1, sizeof *work);
    work->super.callback = preview_load_callback;
    work->super.destroy = preview_load_destroy;

    pv->status = pv->status == PV_LOADING_DELAYED ? PV_LOADING_INITIAL
                                                  : PV_LOADING_NORMAL;
    pv->loading = true;

    work->async = async;
    work->preview = pv;
    work->path = cstr_strdup(preview_path(pv));
    work->width = to_lfm(async)->ui.preview.x;
    work->height = to_lfm(async)->ui.preview.y;
    CHECK_INIT(work->check, to_lfm(async)->loader.preview_cache_version);

    log_trace("loading preview for %s", preview_path_str(pv));
    tpool_add_work(async->tpool, async_preview_load_worker, work, true);
  } else {
    async_lua_preview(async, pv);
  }
}
