#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <libgen.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <string.h>

// zvsiew into static buffer
zsview path_parent_zv(zsview path);

// static buffer
char *dirname_s(const char *path);

zsview basename_zv(zsview path);

// manipulates path
void dirname_cstr(cstr *path);

bool path_isroot(zsview path);

static inline bool path_is_dot_or_dotdot(const char *name) {
  return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
cstr path_replace_tilde(zsview path);

// Allocates a new absolute path with all ~, ., .., // replaced, byob (buffer)
// version. Buffer MUST be of size PATH_MAX+1
// This function only fails if the buffer size is exceeded, returning NULL
char *path_normalize(const char *path, const char *pwd, char *buf,
                     size_t path_len, size_t *len_out);

static inline cstr path_normalize_cstr(const char *path, const char *pwd,
                                       size_t len) {
  char buf[PATH_MAX + 1];
  path_normalize(path, pwd, buf, len, &len);
  return cstr_with_n(buf, len);
}

static inline bool path_is_relative(const char *path) {
  return path[0] != '/';
}

static inline bool path_is_absolute(const char *path) {
  return path[0] == '/';
}
