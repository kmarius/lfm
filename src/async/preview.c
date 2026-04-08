/**
 * struct async_ctx preview loading is annoying because when we fork in another
 * thread, ev will reap the child and we will not get the exit status. Hence, we
 * fork in the main thread, install a child watcher, and, on exit, signal the
 * status to the worker thread.
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

// TODO: maybe we don't need some of these members since we can now acces the
// preview from the other thread
struct preview_check_data {
  struct result super;
  struct async_ctx *async;
  Preview *preview;
  int height;
  int width;
  time_t mtime;
  u64 loadtime;
};

struct preview_load_data {
  struct result super;
  struct async_ctx *async;
  Preview *preview;
  Preview *update;
  ev_child watcher;
  sem_t semaphore;
  int status;
  int fd[2]; // stdout pipe of the process
};

static void preview_check_destroy(void *p) {
  struct preview_check_data *res = p;
  preview_dec_ref(res->preview);
  xfree(res);
}

static void preview_check_callback(void *p, Lfm *lfm) {
  struct preview_check_data *res = p;
  loader_preview_reload(&lfm->loader, res->preview);
}

static void async_preview_check_worker(void *arg) {
  struct preview_check_data *work = arg;
  struct stat statbuf;

  /* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
  if (stat(preview_path(work->preview).str, &statbuf) == -1 ||
      (statbuf.st_mtime <= work->mtime &&
       statbuf.st_mtime <= (long)(work->loadtime / 1000 - 1))) {
    preview_check_destroy(work);
    return;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_preview_check(struct async_ctx *async, Preview *pv) {
  struct preview_check_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &preview_check_callback;
  work->super.destroy = &preview_check_destroy;
  work->super.next = NULL;

  work->async = async;
  work->preview = preview_inc_ref(pv);
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
  ev_child_stop(EV_DEFAULT_ & res->watcher);
  set_ev_child_erase(&res->async->in_progress.previewer_children,
                     &res->watcher);
  preview_dec_ref(res->preview);
  xfree(res);
}

static void preview_load_callback(void *p, Lfm *lfm) {
  struct preview_load_data *res = p;
  preview_update(res->preview, res->update);
  res->update = NULL;
  if (res->preview == lfm->ui.preview.preview)
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
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

void async_preview_load(struct async_ctx *async, Preview *pv) {
  if (pv->status == PV_LOADING_DISOWNED) {
    log_error("not reloading disowned preview %s", preview_path(pv).str);
    return;
  }
  if (!bytes_is_empty(cfg.lua_previewer)) {
    async_lua_preview(async, pv);
  } else if (cstr_is_empty(&cfg.previewer)) {
    log_error("no previewer configured");
  } else {
    struct preview_load_data *work = xcalloc(1, sizeof *work);
    work->super.callback = preview_load_callback;
    work->super.destroy = preview_load_destroy;

    pv->status = pv->status == PV_LOADING_DELAYED ? PV_LOADING_INITIAL
                                                  : PV_LOADING_NORMAL;
    pv->loading = true;

    work->async = async;
    work->preview = preview_inc_ref(pv);

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
    set_ev_child_push(&async->in_progress.previewer_children, &work->watcher);

    log_trace("loading preview for %s", preview_path_str(pv));
    tpool_add_work(async->tpool, async_preview_load_worker, work, true);
  }
}

void async_preview_cancel(struct async_ctx *async) {
  c_foreach(it, set_ev_child, async->in_progress.previewer_children) {
    struct preview_load_data *work =
        container_of(*it.ref, struct preview_load_data, watcher);
    kill(work->watcher.pid, SIGTERM);
    sem_post(&work->semaphore);
    cancel(&work->super);
  }
  set_ev_child_clear(&async->in_progress.previewer_children);

  c_foreach(it, set_result, async->in_progress.lua_previews) {
    cancel(*it.ref);
  }
  set_result_clear(&async->in_progress.lua_previews);
}
