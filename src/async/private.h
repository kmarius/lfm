#include "async.h"
#include "defs.h"
#include "tpool.h"

#include <stdint.h>

struct result {
  struct result *next;
  void (*callback)(void *, struct Lfm *);
  void (*destroy)(void *);
};

struct validity_check8 {
  u8 *ptr;
  u8 val;
};

struct validity_check16 {
  u16 *ptr;
  u16 val;
};

struct validity_check32 {
  u32 *ptr;
  u32 val;
};

struct validity_check64 {
  u64 *ptr;
  u64 val;
};

#define CHECK_INIT(check, value)                                               \
  do {                                                                         \
    static_assert(sizeof((check).val) == sizeof(value), "sizes differ");       \
    (check).ptr = (typeof((check).ptr))&(value);                               \
    (check).val = (typeof((check).val))(value);                                \
  } while (0)

#define CHECK_PASSES(cmp) (*(cmp).ptr == (cmp).val)

void enqueue_and_signal(Async *async, struct result *res);
