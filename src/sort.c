#include "sort.h"

#include "file.h"
#include "strnatcmp.h"

#include <strings.h>

const char *sorttype_str[NUM_SORTTYPE] = {
    "natural", "name", "size", "ctime", "atime", "mtime", "lua", "random",
};

i32 sorttype_from_str(const char *str) {
  for (i32 j = 0; j < NUM_SORTTYPE; j++) {
    if (streq(str, sorttype_str[j]))
      return j;
  }
  return -1;
}

i64 compare_name(const void *a, const void *b) {
  return strcasecmp(file_name_str(*(File **)a), file_name_str(*(File **)b));
}

i64 compare_size(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = file_size(aa) - file_size(bb);
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i64 compare_natural(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = strnatcasecmp(file_name_str(aa), file_name_str(bb));
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i64 compare_ctime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = aa->lstat.st_ctim.tv_sec - bb->lstat.st_ctim.tv_sec;
  if (cmp)
    return cmp;
  cmp = aa->lstat.st_ctim.tv_nsec - bb->lstat.st_ctim.tv_nsec;
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i64 compare_atime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = aa->lstat.st_atim.tv_sec - bb->lstat.st_atim.tv_sec;
  if (cmp)
    return cmp;
  cmp = aa->lstat.st_atim.tv_nsec - bb->lstat.st_atim.tv_nsec;
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i64 compare_mtime(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = aa->lstat.st_mtim.tv_sec - bb->lstat.st_mtim.tv_sec;
  if (cmp)
    return cmp;
  cmp = aa->lstat.st_mtim.tv_nsec - bb->lstat.st_mtim.tv_nsec;
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}

i64 compare_key(const void *a, const void *b) {
  const File *aa = *(File **)a;
  const File *bb = *(File **)b;
  i64 cmp = aa->key - bb->key;
  if (cmp)
    return cmp;
  return aa->lstat.st_ino - bb->lstat.st_ino;
}
