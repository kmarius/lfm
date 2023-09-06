#pragma once

#include <stddef.h>

typedef struct history_s {
  struct history_entry *entries;
  struct history_entry *cur;
  size_t num_old_entries; // entries that were read from the history file
  size_t num_new_entries; // new entries to be written to the file (excluding
                          // ones with leading space)
} History;

void history_load(History *h, const char *path);
void history_write(History *h, const char *path, int histsize);
void history_append(History *h, const char *prefix, const char *line);
void history_reset(History *h);
void history_deinit(History *h);
const char *history_next(History *h);
const char *history_prev(History *h);
