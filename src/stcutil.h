#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <stdarg.h>
#include <string.h>

static inline char *cstr_assign_zv(cstr *self, zsview zv) {
  return cstr_assign_n(self, zv.str, zv.size);
}

static inline char *cstr_append_zv(cstr *self, zsview zv) {
  return cstr_append_n(self, zv.str, zv.size);
}

static inline void cstr_insert_zv(cstr *self, isize pos, zsview zv) {
  cstr_insert_sv(self, pos, zsview_sv(zv));
}

static inline char *cstr_strdup(const cstr *self) {
  return strndup(cstr_str(self), cstr_size(self));
}

static inline char *zsview_strdup(zsview str) {
  return strndup(str.str, str.size);
}

static inline bool cstr_equals_zv(const cstr *self, const zsview *zv) {
  return cstr_size(self) == zv->size &&
         (memcmp(cstr_str(self), zv->str, zv->size) == 0);
}

static inline zsview zsview_from_n(const char *str, size_t n) {
  return (zsview){str, n};
}

// static in cstr_priv.c
static inline isize cstr_vfmt(cstr *self, isize start, const char *fmt,
                              va_list args) {
  va_list args2;
  va_copy(args2, args);
  const int n = vsnprintf(NULL, 0ULL, fmt, args);
  vsnprintf(cstr_reserve(self, start + n) + start, (size_t)n + 1, fmt, args2);
  va_end(args2);
  _cstr_set_size(self, start + n);
  return n;
}
