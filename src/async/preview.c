/**
 * Async preview loading is annoying because when we fork in another thread,
 * ev will reap the child and we will not get the exit status.
 * Hence, we fork in the main thread, install a child watcher, and, on exit,
 * signal the status to the worker thread.
 *
 * Meanwhile, the worker thread is consuming the output of the previewer.
 * When it is signalled the exit status, it can continue e.g. loading an image
 *
 */

#include "private.h"

#include "config.h"
#include "defs.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "loop.h"
#include "memory.h"
#include "preview.h"
#include "stc/cstr.h"
#include "ui.h"

#include <ev.h>

#include <dirent.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#define i_declared
#define i_type vec_ev_child, struct ev_child *
#include "../stc/vec.h"

struct preview_check_data {
  struct result super;
  Async *async;
  char *path;
  int height;
  int width;
  time_t mtime;
  u64 loadtime;
};

struct preview_load_data {
  struct result super;
  Async *async;
  Preview *preview; // not guaranteed to exist, do not touch
  Preview *update;
  ev_child watcher;
  sem_t semaphore;
  int status;
  int fd[2]; // stdout pipe of the process
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
  work->path = zsview_strdup(preview_path(pv));
  work->height = pv->height;
  work->width = pv->width;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;

  log_trace("checking preview %s", preview_path_str(pv));
  tpool_add_work(async->tpool, async_preview_check_worker, work, true);
}

static void preview_load_destroy(void *p) {
  struct preview_load_data *res = p;
  if (res->fd[0] > 0)
    close(res->fd[0]);
  sem_destroy(&res->semaphore);
  preview_destroy(res->update);
  c_foreach(it, vec_ev_child, res->async->previewer_children) {
    if (*it.ref == &res->watcher) {
      vec_ev_child_erase_at(&res->async->previewer_children, it);
      break;
    }
  }
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

  log_trace("reading preview output: %s", cstr_str(&work->update->path));
  preview_read_output(work->update, work->fd);

  log_trace("waiting for signal");
  sem_wait(&work->semaphore);
  // exit status stored in work->status
  log_trace("previewer status code after signal: %d", work->status);

  preview_handle_exit_status(work->update, work->status);
  log_trace("finished preview: %s", cstr_str(&work->update->path));

  enqueue_and_signal(work->async, (struct result *)work);
}

static void child_exit_cb(EV_P_ ev_child *w, int revents) {
  (void)revents;
  struct preview_load_data *work =
      container_of(w, struct preview_load_data, watcher);

  work->status = w->rstatus;
  sem_post(&work->semaphore);

  ev_child_stop(EV_A_ w);
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

    // we could modify these to load extra lines, but we would need to make
    // changes because we resize images to thexe exact dimensions
    u32 width = to_lfm(async)->ui.preview.x;
    u32 height = to_lfm(async)->ui.preview.y;

    // first stage of loading the preview: fork the previewer process
    pid_t pid = 0;
    work->update = preview_fork_previewer(cstr_zv(&pv->path), width, height,
                                          &pid, work->fd);
    // TODO: we could handle some errors here

    // install the child watcher and set up signal
    ev_child_init(&work->watcher, child_exit_cb, pid, 0);
    ev_child_start(event_loop, &work->watcher);
    sem_init(&work->semaphore, 0, 0);
    vec_ev_child_push(&async->previewer_children, &work->watcher);

    CHECK_INIT(work->check, to_lfm(async)->loader.preview_cache_version);

    log_trace("loading preview for %s", preview_path_str(pv));
    tpool_add_work(async->tpool, async_preview_load_worker, work, true);
  } else {
    async_lua_preview(async, pv);
  }
}

void async_kill_previewers(Async *async) {
  c_foreach(it, vec_ev_child, async->previewer_children) {
    struct preview_load_data *work =
        container_of(*it.ref, struct preview_load_data, watcher);
    kill(work->watcher.pid, SIGTERM);
    sem_post(&work->semaphore);
  }
  vec_ev_child_drop(&async->previewer_children);
}
