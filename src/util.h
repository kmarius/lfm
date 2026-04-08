#pragma once

#include "defs.h"

#include <stc/zsview.h>

#include <stdbool.h>
#include <stdlib.h>

#include <bits/types.h>

#ifndef streq
#define streq(X, Y) (*(char *)(X) == *(char *)(Y) && strcmp(X, Y) == 0)
#endif

static inline i32 min(i32 i, i32 j) {
  return i < j ? i : j;
}

static inline i32 max(i32 i, i32 j) {
  return i > j ? i : j;
}

char *rtrim(char *s);

char *ltrim(char *s);

static inline char *trim(char *s) {
  return ltrim(rtrim(s));
}

char *strcasestr(const char *str, const char *sub);

bool hascaseprefix(const char *str, const char *pre);

char *readable_filesize(f64 size, char *buf);

u64 current_micros(void);

u64 current_millis(void);

// recursive mkdir
i32 mkdir_p(char *path, __mode_t mode);

// make all directory components of the file at path
i32 make_dirs(zsview path, __mode_t mode);

// Writes the mimetype of the file at PATH into the buffer dest of length sz.
// Returns true on success, false on failure with *dest == '\0'
bool get_mimetype(const char *path, char *dest, usize sz);

bool valgrind_active(void);

static inline zsview getenv_zv(const char *name) {
  char *val = getenv(name);
  if (unlikely(val == NULL)) {
    return c_zv("");
  }
  return zsview_from(val);
}

// case insensitive cmp, but 'a' < 'A'
i32 strcasecmp_strict(const char *s1, const char *s2);

// returns -1 if the buffer is too short
i32 shorten_name(zsview name, i32 max_len, bool has_ext, char *buf,
                 usize bufsz);
