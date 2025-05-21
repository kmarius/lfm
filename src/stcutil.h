#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"
#include <string.h>

static inline char *cstr_assign_zv(cstr *self, zsview other) {
  return cstr_assign_n(self, other.str, other.size);
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
