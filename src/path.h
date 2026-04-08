#pragma once

#include "defs.h"

#include <stc/cstr.h>
#include <stc/zsview.h>

#include <stdbool.h>

#include <libgen.h>
#include <linux/limits.h>

// zvsiew into static buffer
zsview path_parent_zv(zsview path);

// zvsiew into static buffer
static inline zsview path_parent(zsview path) {
  return path_parent_zv(path);
}

// subview into path, or c_zv(".")
zsview basename_zv(zsview path);

// zview into path, or c_zv(".")
static inline zsview basename_cstr(const cstr *path) {
  return basename_zv(cstr_zv(path));
}

isize path_concat(zsview dir, zsview name, char *buf, usize bufsz);

// manipulates path
void dirname_cstr(cstr *path);

static inline bool path_is_root(zsview path) {
  return !zsview_is_empty(path) && path.str[0] == '/' && path.str[1] == 0;
}

static inline bool path_is_dot_or_dotdot(const char *name) {
  return name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
cstr path_replace_tilde(zsview path);

// Normalizes path by replacing all ~/, ., .., //
// This function only fails if the buffer size is exceeded, returning c_zv("")
zsview path_normalize3(zsview path, const char *pwd, char *buf, usize bufsz);

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

// returns the length of the contents of buf (excluding nul), -1 if buffer too
// small
isize path_make_absolute(zsview path, char *buf, usize bufsz);

// returns a zview into name
zsview name_ext(const zsview *name);
