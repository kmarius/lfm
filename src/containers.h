#pragma once

#include "memory.h"

#include "stc/cstr.h"

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

#define i_type vec_bytes, struct bytes
#define i_keyclone(p) ((struct bytes){memdup(p.data, p.len), p.len})
#define i_keydrop(p) (xfree(p->data))
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
#define i_keydrop(p) (xfree((p)->key), xfree((p)->val))
#define i_keyclone i_keyfrom
#include "stc/vec.h"
