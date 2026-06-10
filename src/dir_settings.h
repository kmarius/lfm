#pragma once

// Extracted from sort.h and dir.h to break circular dependencies.
// This header can be included by config.h without pulling in the entire
// dir.h dependency tree (file.h, filter.h, loadable.h, etc.)

#include "defs.h"

#include <stdbool.h>

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

typedef enum {
  INFO_SIZE = 0,
  INFO_ATIME,
  INFO_CTIME,
  INFO_MTIME,
  NUM_FILEINFO
} fileinfo;

extern const char *fileinfo_str[NUM_FILEINFO];

i32 fileinfo_from_str(const char *str);

struct dir_settings {
  bool hidden;
  bool dirfirst;
  bool reverse;
  sorttype sorttype;
  fileinfo fileinfo;
  u64 salt; // used for random sort
};
