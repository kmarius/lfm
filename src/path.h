#pragma once

#include "stc/zsview.h"

#include <stdbool.h>
#include <string.h>

// these return pointer to statically allocated arrays
char *realpath_s(const char *p);

char *basename_s(const char *p);

char *dirname_s(const char *p);

zsview path_parent_s(zsview path);

bool path_isroot(zsview path);

static inline bool path_is_dot_or_dotdot(const char *name) {
  return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

static inline char *realpath_a(const char *p) {
  return strdup(realpath_s(p));
}

static inline char *basename_a(const char *p) {
  return strdup(basename_s(p));
}

static inline char *dirname_a(const char *p) {
  return strdup(dirname_s(p));
}

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
cstr path_replace_tilde(zsview path);

// Allocates a new absolute path with all ~, ., .., // replaced
// This function only fails if the buffer size is exceeded, returning NULL
char *path_normalize_a(const char *path, const char *pwd, size_t path_len,
                       size_t *len_out);

// Allocates a new absolute path with all ~, ., .., // replaced, byob (buffer)
// version. Buffer MUST be of size PATH_MAX+1
// This function only fails if the buffer size is exceeded, returning NULL
char *path_normalize(const char *path, const char *pwd, char *buf,
                     size_t path_len, size_t *len_out);

static inline bool path_is_relative(const char *path) {
  return path[0] != '/';
}

static inline bool path_is_absolute(const char *path) {
  return path[0] == '/';
}
