#pragma once

#include <stddef.h>

#include "hashtab.h"

typedef struct history_s {
  LinkedHashtab items;
  struct lht_bucket *cur;
  size_t num_new_entries; // new entries to be written to the file (excluding
                          // ones with leading space)
} History;

// Prefix contains an allocated string, line points to right after its nul byte.
struct history_entry {
  char *prefix;
  const char *line;
  bool is_new;
};

void history_load(History *h, const char *path);
void history_write(History *h, const char *path, int histsize);
void history_append(History *h, const char *prefix, const char *line);
void history_reset(History *h);
void history_deinit(History *h);
const char *history_next(History *h);
const char *history_prev(History *h);
