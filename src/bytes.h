#pragma once

#include "macros.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>

// A slice of bytes with fixed length.
typedef struct bytes {
  char *buf;
  size_t size;
} bytes;

static inline bytes bytes_init() {
  return (bytes){0};
}

static inline bytes bytes_from_n(const char *data, size_t size) {
  if (unlikely(size == 0)) {
    return bytes_init();
  }
  char *data_ = memdup(data, size);
  return (bytes){data_, data_ ? size : 0};
}

static inline bytes bytes_clone(bytes bs) {
  return bytes_from_n(bs.buf, bs.size);
}

static inline bytes bytes_move(bytes *self) {
  struct bytes res = *self;
  memset(self, 0, sizeof *self);
  return res;
}

static inline void bytes_drop(bytes *self) {
  if (self) {
    free(self->buf);
  }
}

static inline bool bytes_is_empty(bytes bs) {
  return bs.size == 0;
}
