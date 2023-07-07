#pragma once

#include <bits/types.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "memory.h"

#ifndef streq
#define streq(X, Y) (*(char *)(X) == *(char *)(Y) && strcmp(X, Y) == 0)
#endif

#ifndef strcaseeq
#define strcaseeq(X, Y) (strcasecmp(X, Y) == 0)
#endif

static inline char *rtrim(char *s)
{
  char *t = s;
  char *end = s - 1;
  while (*t) {
    if (!isspace(*t)) {
      end = t;
    }
    t++;
  }
  *++end = 0;
  return s;
}


static inline char *ltrim(char *s)
{
  s--;
  while (isspace(*++s)) {}
  return s;
}


static inline char *trim(char *s)
{
  return ltrim(rtrim(s));
}


static inline int min(int i, int j)
{
  return i < j ? i : j;
}


static inline int max(int i, int j)
{
  return i > j ? i : j;
}


bool haswprefix(const wchar_t *restrict string, const wchar_t *restrict prefix);

bool haswcaseprefix(const wchar_t *restrict string, const wchar_t *restrict prefix);

const wchar_t *wstrcasestr(const wchar_t *str, const wchar_t *sub);

const char *strcasestr(const char *str, const char *sub);

const char *strcaserchr(const char *str, char c);

bool hasprefix(const char *str, const char *pre);

bool hascaseprefix(const char *str, const char *pre);

bool hassuffix(const char *suf, const char *str);

bool hascasesuffix(const char *suf, const char *str);

char *readable_filesize(double size, char *buf);

int msleep(uint32_t msec);

uint64_t current_micros(void);

uint64_t current_millis(void);

// recursive mkdir
int mkdir_p(char *path, __mode_t mode);

int asprintf(char **dst, const char *format, ...);

int vasprintf(char **dst, const char *format, va_list args);

// converts mb string s to a newly allocated wchar string, optionally passes
// the length to len
ALLOC wchar_t *ambstowcs(const char *s, int *len);

static inline size_t mbslen(const char *s)
{
  return mbstowcs(NULL, s, 0);
}

// these return pointer to statically allocated arrays
char *realpath_s(const char *p);

char *basename_s(const char *p);

char *dirname_s(const char *p);

ALLOC static inline char *realpath_a(const char *p)
{
  return strdup(realpath_s(p));
}

ALLOC static inline char *basename_a(const char *p)
{
  return strdup(basename_s(p));
}

ALLOC static inline char *dirname_a(const char *p)
{
  return strdup(dirname_s(p));
}

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
ALLOC char *path_replace_tilde(const char* path);

// Allocates a new absolute path with all ~, ., .., // replaced
ALLOC char *path_qualify(const char* path, const char *pwd);

static inline bool path_is_relative(const char *path)
{
  return *path != '/';
}

static inline bool path_is_absolute(const char *path)
{
  return *path == '/';
}

// Writes the mimetype of the file at PATH into the buffer dest of length sz.
// Returns true on success, false on failure with *dest == '\0'
bool get_mimetype(const char *path, char *dest, size_t sz);

bool valgrind_active(void);
