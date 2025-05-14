#pragma once

#include "memory.h"

#define i_header
#define i_type vec_int, int
#include "stc/vec.h"

#define i_header
#define i_type vec_str, char *
#define i_keyraw const char *
#define i_keyfrom(p) (strdup(p))
#define i_keytoraw(p) (*(p))
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
