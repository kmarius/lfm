#include "../tpool.h"
#include "async.h"

#include <stdint.h>

struct result {
  struct result *next;
  void (*callback)(void *, struct Lfm *);
  void (*destroy)(void *);
};

struct validity_check8 {
  uint8_t *ptr;
  uint8_t val;
};

struct validity_check16 {
  uint16_t *ptr;
  uint16_t val;
};

struct validity_check32 {
  uint32_t *ptr;
  uint32_t val;
};

struct validity_check64 {
  uint64_t *ptr;
  uint64_t val;
};

#define CHECK_INIT(check, value)                                               \
  do {                                                                         \
    static_assert(sizeof((check).val) == sizeof(value), "sizes differ");       \
    (check).ptr = (typeof((check).ptr))&(value);                               \
    (check).val = (typeof((check).val))(value);                                \
  } while (0)

#define CHECK_PASSES(cmp) (*(cmp).ptr == (cmp).val)

void enqueue_and_signal(Async *async, struct result *res);
