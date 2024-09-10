#include "sort.h"

#include "file.h"
#include "memory.h"
#include "strnatcmp.h"

#include <strings.h> // strcasecmp

const char *sorttype_str[NUM_SORTTYPE] = {
    "natural", "name", "size", "ctime", "atime", "mtime", "random",
};

int compare_name(const void *a, const void *b) {
  return strcasecmp(file_name(*(File **)a), file_name(*(File **)b));
}

int compare_size(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  long cmp = file_size(aa) - file_size(bb);
  if (cmp) {
    // conversion from long to int breaks this for large files
    return cmp < 0 ? -1 : 1;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

int compare_natural(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  int cmp = strnatcasecmp(file_name(aa), file_name(bb));
  if (cmp) {
    return cmp;
  }
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

int compare_ctime(const void *a, const void *b) {
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

int compare_atime(const void *a, const void *b) {
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

int compare_mtime(const void *a, const void *b) {
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

// https://stackoverflow.com/questions/6127503/shuffle-array-in-c
// arrange the N elements of ARRAY in random order.
// Only effective if N is much smaller than RAND_MAX;
// if this may not be the case, use a better random
// number generator.
void shuffle(void *arr_, size_t n, size_t size) {
  if (n <= 1) {
    return;
  }

  char *arr = arr_;
  char *tmp = xmalloc(n * size);

  for (size_t i = 0; i < n - 1; ++i) {
    const size_t rnd = (size_t)rand();
    size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

    memcpy(tmp, arr + j * size, size);
    memcpy(arr + j * size, arr + i * size, size);
    memcpy(arr + i * size, tmp, size);
  }

  xfree(tmp);
}
