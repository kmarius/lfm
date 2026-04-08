#include "sort.h"

#include "file.h"
#include "memory.h"
#include "strnatcmp.h"

#include <strings.h>

const char *sorttype_str[NUM_SORTTYPE] = {
    "natural", "name", "size", "ctime", "atime", "mtime", "lua", "random",
};

i32 sorttype_from_str(const char *str) {
  for (i32 j = 0; j < NUM_SORTTYPE; j++) {
    if (streq(str, sorttype_str[j])) {
      return j;
    }
  }
  return -1;
}

i32 compare_name(const void *a, const void *b) {
  return strcasecmp(file_name_str(*(File **)a), file_name_str(*(File **)b));
}

i32 compare_size(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  long cmp = file_size(aa) - file_size(bb);
  if (cmp) {
    // conversion from long to i32 breaks this for large files
    return cmp < 0 ? -1 : 1;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i32 compare_natural(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i32 cmp = strnatcasecmp(file_name_str(aa), file_name_str(bb));
  if (cmp) {
    return cmp;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i32 compare_ctime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  long cmp = aa->lstat.st_ctim.tv_sec - bb->lstat.st_ctim.tv_sec;
  if (cmp) {
    return cmp;
  }
  cmp = aa->lstat.st_ctim.tv_nsec - bb->lstat.st_ctim.tv_nsec;
  if (cmp) {
    return cmp;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i32 compare_atime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  long cmp = aa->lstat.st_atim.tv_sec - bb->lstat.st_atim.tv_sec;
  if (cmp) {
    return cmp;
  }
  cmp = aa->lstat.st_atim.tv_nsec - bb->lstat.st_atim.tv_nsec;
  if (cmp) {
    return cmp;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i32 compare_mtime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  long cmp = aa->lstat.st_mtim.tv_sec - bb->lstat.st_mtim.tv_sec;
  if (cmp) {
    return cmp;
  }
  cmp = aa->lstat.st_mtim.tv_nsec - bb->lstat.st_mtim.tv_nsec;
  if (cmp) {
    return cmp;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i32 compare_lua(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  return aa->key - bb->key;
}

// https://stackoverflow.com/questions/6127503/shuffle-array-in-c
// arrange the N elements of ARRAY in random order.
// Only effective if N is much smaller than RAND_MAX;
// if this may not be the case, use a better random
// number generator.
void shuffle(void *arr_, usize n, usize size) {
  if (n <= 1) {
    return;
  }

  char *arr = arr_;
  char *tmp = xmalloc(size);

  for (usize i = 0; i < n - 1; ++i) {
    const usize rnd = (usize)rand();
    usize j = i + rnd / (RAND_MAX / (n - i) + 1);

    memcpy(tmp, arr + j * size, size);
    memcpy(arr + j * size, arr + i * size, size);
    memcpy(arr + i * size, tmp, size);
  }

  xfree(tmp);
}
