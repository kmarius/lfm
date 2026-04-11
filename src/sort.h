#pragma once

#include "defs.h"

#include <stddef.h>

typedef enum {
  SORT_NATURAL = 0,
  SORT_NAME,
  SORT_SIZE,
  SORT_CTIME,
  SORT_ATIME,
  SORT_MTIME,
  SORT_LUA,
  SORT_RAND,
  NUM_SORTTYPE,
} sorttype;

extern const char *sorttype_str[NUM_SORTTYPE];

i32 sorttype_from_str(const char *str);

i64 compare_name(const void *a, const void *b);

i64 compare_size(const void *a, const void *b);

i64 compare_natural(const void *a, const void *b);

i64 compare_ctime(const void *a, const void *b);

i64 compare_atime(const void *a, const void *b);

i64 compare_mtime(const void *a, const void *b);

i64 compare_lua(const void *a, const void *b);

void shuffle(void *arr, usize n, usize size);
