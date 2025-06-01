#pragma once

#include "macros.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>

// A slice of bytes with fixed length.
typedef struct bytes {
  char *data;
  size_t size;
} bytes;

static inline struct bytes bytes_init() {
  return (struct bytes){0};
}

static inline struct bytes bytes_from_n(const char *bytes, size_t len) {
  if (unlikely(len == 0)) {
    return bytes_init();
  }
  char *data = memdup(bytes, len);
  return (struct bytes){data, data ? len : 0};
}

static inline struct bytes bytes_clone(struct bytes bytes) {
  return bytes_from_n(bytes.data, bytes.size);
}

static inline struct bytes bytes_move(struct bytes *bytes) {
  struct bytes res = *bytes;
  memset(bytes, 0, sizeof *bytes);
  return res;
}

static inline void bytes_drop(struct bytes *bytes) {
  if (bytes) {
    free(bytes->data);
  }
}

static inline bool bytes_is_empty(struct bytes bytes) {
  return bytes.size == 0;
}
