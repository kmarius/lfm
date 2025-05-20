#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"
#include <string.h>

static inline bool cstr_equals_zv(const cstr *self, const zsview *zv) {
  return cstr_size(self) == zv->size &&
         (memcmp(cstr_str(self), zv->str, zv->size) == 0);
}

static inline zsview zsview_from_n(const char *str, size_t n) {
  return (zsview){str, n};
}
