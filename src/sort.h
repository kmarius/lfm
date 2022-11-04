#pragma once

#include <stddef.h>

typedef enum sorttype_e {
  SORT_NATURAL,
  SORT_NAME,
  SORT_SIZE,
  SORT_CTIME,
  SORT_RAND,
} sorttype;

int compare_name(const void *a, const void *b);

int compare_size(const void *a, const void *b);

int compare_natural(const void *a, const void *b);

int compare_ctime(const void *a, const void *b);

void shuffle(void *arr, size_t n, size_t size);
