#include "async.h"
#include "defs.h"
#include "tpool.h"

#include <stdatomic.h>

struct result {
  struct result *next;
  // atomic_bool cancelled;
  bool cancelled;
  void (*callback)(void *, struct Lfm *);
  void (*destroy)(void *);
};

#define i_declared
#define i_type set_result, struct result *
#include <stc/hset.h>

#define i_declared
#define i_type set_ev_child, struct ev_child *
#include <stc/hset.h>

// we only cancel from the main thread
static inline void cancel(struct result *res) {
  if (res)
    res->cancelled = true;
  // atomic_store_explicit(&res->cancelled, memory_order_relaxed, true);
}

static inline bool is_cancelled(struct result *res) {
  // return atomic_load_explicit(&res->cancelled, memory_order_relaxed);
  return res->cancelled;
}

void enqueue_and_signal(struct async_ctx *async, struct result *res);
