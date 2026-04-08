#include "private.h"

#include "defs.h"
#include "lfm.h" // to_lfm
#include "loop.h"
#include "stc/cstr.h"

#include <ev.h>

#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

static struct result *result_queue_get(struct result_queue *queue);

static void async_result_cb(EV_P_ ev_async *w, int revents) {
  (void)revents;
  struct async_ctx *async = w->data;

  struct result *res;
  while ((res = result_queue_get(&async->queue))) {
    if (!is_cancelled(res))
      res->callback(res, to_lfm(async));
    res->destroy(res);
  }
}

void async_ctx_init(struct async_ctx *async) {
  memset(async, 0, sizeof *async);

  pthread_mutex_init(&async->queue.mutex, NULL);

  ev_async_init(&async->result_watcher, async_result_cb);
  ev_async_start(event_loop, &async->result_watcher);

  if (pthread_mutex_init(&async->queue.mutex, NULL) != 0) {
    perror("pthread_mutex_init");
    exit(EXIT_FAILURE);
  }

  async->result_watcher.data = async;

  ev_async_send(EV_DEFAULT_ & async->result_watcher);

  const usize nthreads = get_nprocs() + 1;
  async->tpool = tpool_create(nthreads);
}

void async_ctx_deinit(struct async_ctx *async) {
  atomic_store_explicit(&async->stop, 1, memory_order_relaxed);
  async_preview_cancel(async);
  set_result_drop(&async->in_progress.lua_previews);
  set_result_drop(&async->in_progress.inotify);
  set_result_drop(&async->in_progress.dirs);
  set_ev_child_drop(&async->in_progress.previewer_children);

  tpool_wait(async->tpool);
  tpool_destroy(async->tpool);

  struct result *res;
  while ((res = result_queue_get(&async->queue)))
    res->destroy(res);
  pthread_mutex_destroy(&async->queue.mutex);
}

static inline void result_queue_put(struct result_queue *queue,
                                    struct result *res) {
  pthread_mutex_lock(&queue->mutex);
  if (!queue->head) {
    queue->head = res;
    queue->tail = res;
  } else {
    queue->tail->next = res;
    queue->tail = res;
  }
  pthread_mutex_unlock(&queue->mutex);
}

static inline struct result *result_queue_get(struct result_queue *queue) {
  pthread_mutex_lock(&queue->mutex);
  struct result *res = queue->head;
  if (res) {
    queue->head = res->next;
    res->next = NULL;
    if (queue->tail == res)
      queue->tail = NULL;
  }
  pthread_mutex_unlock(&queue->mutex);
  return res;
}

void enqueue_and_signal(struct async_ctx *async, struct result *res) {
  result_queue_put(&async->queue, res);
  ev_async_send(EV_DEFAULT_ & async->result_watcher);
}
