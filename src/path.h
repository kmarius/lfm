#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <libgen.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <string.h>

// zvsiew into static buffer
zsview path_parent_zv(zsview path);

// zvsiew into static buffer
static inline zsview path_parent(const cstr *path) {
  return path_parent_zv(cstr_zv(path));
}

// subview into path, or c_zv(".")
zsview basename_zv(zsview path);

// zview into path, or c_zv(".")
static inline zsview basename_cstr(const cstr *path) {
  return basename_zv(cstr_zv(path));
}

// manipulates path
void dirname_cstr(cstr *path);

static inline bool path_is_root_zv(zsview path) {
  return !zsview_is_empty(path) && path.str[0] == '/' && path.str[1] == 0;
}

static inline bool path_is_root(const cstr *path) {
  return path_is_root_zv(cstr_zv(path));
}

static inline bool path_is_dot_or_dotdot(const char *name) {
  return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
cstr path_replace_tilde(zsview path);

// Normalizes path by replacing all ~/, ., .., //
// This function only fails if the buffer size is exceeded, returning c_zv("")
zsview path_normalize3(zsview path, const char *pwd, char *buf, size_t bufsz);

static inline cstr path_normalize_cstr(zsview path, const char *pwd) {
  char buf[PATH_MAX + 1];
  return cstr_from_zv(path_normalize3(path, pwd, buf, sizeof buf));
}

static inline bool path_is_relative(const char *path) {
  return path[0] != '/';
}

static inline bool path_is_absolute(const char *path) {
  return path[0] == '/';
}

static inline bool path_is_absolute_zv(zsview path) {
  return path.str[0] == '/';
}
