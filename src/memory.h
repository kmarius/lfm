#pragma once

#include "defs.h"

#include <stdlib.h>
#include <string.h>

// possibly extended with something useful in the future

#ifndef ALLOC
#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))
#endif

ALLOC static inline void *xmalloc(usize size) {
  return malloc(size);
}

ALLOC static inline void *xcalloc(usize nmemb, usize size) {
  return calloc(nmemb, size);
}

ALLOC static inline void *xrealloc(void *ptr, usize size) {
  return realloc(ptr, size);
}

static inline void xfree(void *p) {
  free(p);
}

usize xstrlcpy(char *restrict dst, const char *restrict src, usize dsize);

void strchrsub(char *str, char c, char x);

static inline void *memdup(const void *src, usize n) {
  void *mem = malloc(n);
  if (unlikely(mem == NULL))
    return NULL;
  return memcpy(mem, src, n);
}
