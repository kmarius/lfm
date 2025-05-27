#include "private.h"

#include "../lfm.h"
#include "../log.h"
#include "../macros_defs.h"
#include "../stc/cstr.h"
#include "../ui.h"

#include <ev.h>

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

static struct result *result_queue_get(struct result_queue *queue);

static void async_result_cb(EV_P_ ev_async *w, int revents) {
  (void)revents;
  Async *async = w->data;
  struct result *res;

  pthread_mutex_lock(&async->queue.mutex);
  while ((res = result_queue_get(&async->queue))) {
    res->callback(res, to_lfm(async));
  }
  pthread_mutex_unlock(&async->queue.mutex);

  ev_idle_start(EV_A_ & to_lfm(async)->ui.redraw_watcher);
}

void async_init(Async *async) {
  async->queue.head = NULL;
  async->queue.tail = NULL;
  pthread_mutex_init(&async->queue.mutex, NULL);

  ev_async_init(&async->result_watcher, async_result_cb);
  ev_async_start(to_lfm(async)->loop, &async->result_watcher);

  if (pthread_mutex_init(&async->queue.mutex, NULL) != 0) {
    log_error("pthread_mutex_init: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  async->result_watcher.data = async;

  ev_async_send(EV_DEFAULT_ & async->result_watcher);

  const size_t nthreads = get_nprocs() + 1;
  async->tpool = tpool_create(nthreads);
}

void async_deinit(Async *async) {
  tpool_wait(async->tpool);
  tpool_destroy(async->tpool);

  struct result *res;
  while ((res = result_queue_get(&async->queue))) {
    res->destroy(res);
  }
  pthread_mutex_destroy(&async->queue.mutex);
}

static inline void result_queue_put(struct result_queue *t,
                                    struct result *res) {
  if (!t->head) {
    t->head = res;
    t->tail = res;
  } else {
    t->tail->next = res;
    t->tail = res;
  }
}

static inline struct result *result_queue_get(struct result_queue *t) {
  struct result *res = t->head;

  if (!res) {
    return NULL;
  }

  t->head = res->next;
  res->next = NULL;
  if (t->tail == res) {
    t->tail = NULL;
  }

  return res;
}

void enqueue_and_signal(Async *async, struct result *res) {
  pthread_mutex_lock(&async->queue.mutex);
  result_queue_put(&async->queue, res);
  pthread_mutex_unlock(&async->queue.mutex);
  ev_async_send(EV_DEFAULT_ & async->result_watcher);
}
