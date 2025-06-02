#pragma once

#include "macros.h"

#include "stc/zsview.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <bits/types.h>

#ifndef streq
#define streq(X, Y) (*(char *)(X) == *(char *)(Y) && strcmp(X, Y) == 0)
#endif

static inline int min(int i, int j) {
  return i < j ? i : j;
}

static inline int max(int i, int j) {
  return i > j ? i : j;
}

char *rtrim(char *s);

char *ltrim(char *s);

static inline char *trim(char *s) {
  return ltrim(rtrim(s));
}

const wchar_t *wstrcasestr(const wchar_t *str, const wchar_t *sub);

char *strcasestr(const char *str, const char *sub);

bool hascaseprefix(const char *str, const char *pre);

char *readable_filesize(double size, char *buf);

uint64_t current_micros(void);

uint64_t current_millis(void);

// recursive mkdir
int mkdir_p(char *path, __mode_t mode);

// make all directory components of the file at path
int make_dirs(zsview path, __mode_t mode);

// converts mb string s to a newly allocated wchar string, optionally passes
// the length to len
wchar_t *ambstowcs(const char *s, int *len);

// Writes the mimetype of the file at PATH into the buffer dest of length sz.
// Returns true on success, false on failure with *dest == '\0'
bool get_mimetype(const char *path, char *dest, size_t sz);

bool valgrind_active(void);

static inline zsview getenv_zv(const char *name) {
  char *val = getenv(name);
  if (unlikely(val == NULL)) {
    return c_zv("");
  }
  return zsview_from(val);
}

// case insensitive cmp, but 'a' < 'A'
int strcasecmp_strict(const char *s1, const char *s2);
