#pragma once

typedef struct env_entry_raw {
  const char *key;
  const char *val;
} env_entry_raw;

typedef struct env_entry {
  char *key;
  char *val;
} env_entry;

#define i_header
#define i_type vec_env, struct env_entry
#define i_keyraw struct env_entry_raw
#define i_keytoraw(p) ((struct env_entry_raw){(p)->key, (p)->val})
#define i_keyfrom(p) ((struct env_entry){strdup((p).key), strdup((p).val)})
#define i_keydrop(p) (free((p)->key), free((p)->val))
#define i_keyclone i_keyfrom
#include "stc/vec.h"
