#pragma once

#include <stdbool.h>
#include <string.h>

// these return pointer to statically allocated arrays
char *realpath_s(const char *p);

char *basename_s(const char *p);

char *dirname_s(const char *p);

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
char *path_replace_tilde(const char *path);

// Allocates a new absolute path with all ~, ., .., // replaced
char *path_normalize_a(const char *path, const char *pwd);

// Allocates a new absolute path with all ~, ., .., // replaced, byob (bring
// your own buffer) version.
char *path_normalize(const char *path, const char *pwd, char *buf);

static inline bool path_is_relative(const char *path) {
  return path[0] != '/';
}

static inline bool path_is_absolute(const char *path) {
  return path[0] == '/';
}
