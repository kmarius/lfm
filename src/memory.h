#pragma once

#include <stdlib.h>
#include <string.h>

// possibly extended with something useful in the future

#ifndef ALLOC
#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))
#endif

ALLOC static inline void *xmalloc(size_t size) {
  return malloc(size);
}

ALLOC static inline void *xcalloc(size_t nmemb, size_t size) {
  return calloc(nmemb, size);
}

ALLOC static inline void *xrealloc(void *ptr, size_t size) {
  return realloc(ptr, size);
}

static inline void xfree(void *p) {
  free(p);
}

size_t xstrlcpy(char *restrict dst, const char *restrict src, size_t dsize);

void strchrsub(char *str, char c, char x);

static inline void *memdup(const void *src, size_t n) {
  void *mem = malloc(n);
  if (mem == NULL)
    return NULL;
  return memcpy(mem, src, n);
}
