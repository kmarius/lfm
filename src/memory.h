#pragma once

#include <stdlib.h>

// possibly extended with something useful in the future

#define ALLOC __attribute__((malloc)) __attribute__((warn_unused_result))

ALLOC static inline void *xmalloc(size_t size)
{
  return malloc(size);
}

ALLOC static inline void *xcalloc(size_t nmemb, size_t size)
{
  return calloc(nmemb, size);
}

ALLOC static inline void *xrealloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}

static inline void xfree(void *p)
{
  free(p);
}

#define CLEAR(ptr)  \
  do { \
    ptr = NULL; \
  } while (0)

#define XFREE_CLEAR(ptr)  \
  do { \
    void **ptr_ = (void **) &(ptr); \
    xfree(*ptr_); \
    CLEAR(*ptr_); \
    (void)(*ptr_); \
  } while (0)
