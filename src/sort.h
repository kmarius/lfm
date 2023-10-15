#pragma once

#include <stddef.h>

typedef enum {
  SORT_NATURAL = 0,
  SORT_NAME,
  SORT_SIZE,
  SORT_CTIME,
  SORT_ATIME,
  SORT_MTIME,
  SORT_RAND,
  NUM_SORTTYPE,
} sorttype;

extern const char *sorttype_str[NUM_SORTTYPE];

int compare_name(const void *a, const void *b);

int compare_size(const void *a, const void *b);

int compare_natural(const void *a, const void *b);

int compare_ctime(const void *a, const void *b);

int compare_atime(const void *a, const void *b);

int compare_mtime(const void *a, const void *b);

void shuffle(void *arr, size_t n, size_t size);
