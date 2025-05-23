#pragma once

#include "macros_defs.h"
#include "memory.h"

#include "stc/cstr.h"
#include <string.h>

#define i_header
#define i_type vec_int, int
#include "stc/vec.h"

#define i_header
#define i_type vec_str, char *
#define i_keyraw const char *
#define i_keyfrom(p) (strdup(p))
#define i_keytoraw(p) (*(p))
#include "stc/vec.h"

#define i_keypro cstr
#include "stc/vec.h"

struct bytes {
  char *data;
  size_t len;
};

static inline struct bytes bytes_init() {
  return (struct bytes){0};
}

static inline struct bytes bytes_from_n(const char *bytes, size_t len) {
  if (unlikely(len == 0)) {
    return bytes_init();
  }
  char *data = memdup(bytes, len);
  return ((struct bytes){data, data ? len : 0});
}

static inline struct bytes bytes_clone(struct bytes bytes) {
  return bytes_from_n(bytes.data, bytes.len);
}

static inline struct bytes bytes_from_str(const char *str) {
  int len = strlen(str);
  return ((struct bytes){strndup(str, len), len});
}

static inline struct bytes bytes_move(struct bytes *bytes) {
  struct bytes res = *bytes;
  memset(bytes, 0, sizeof *bytes);
  return res;
}

static inline void bytes_drop(struct bytes *bytes) {
  if (bytes) {
    free(bytes->data);
  }
}

static inline bool bytes_is_empty(struct bytes bytes) {
  return bytes.len == 0;
}

#define i_type vec_bytes, struct bytes
#define i_keyclone bytes_clone
#define i_keydrop bytes_drop
#include "stc/vec.h"

struct env_entry_raw {
  const char *key;
  const char *val;
};

struct env_entry {
  char *key;
  char *val;
};

#define i_type env_list, struct env_entry
#define i_keyraw struct env_entry_raw
#define i_keytoraw(p) ((struct env_entry_raw){(p)->key, (p)->val})
#define i_keyfrom(p) ((struct env_entry){strdup((p).key), strdup((p).val)})
#define i_keydrop(p) (free((p)->key), free((p)->val))
#define i_keyclone i_keyfrom
#include "stc/vec.h"
