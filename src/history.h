#pragma once

#include "stc/forward.h"

#include <stdbool.h>
#include <stddef.h>

// Prefix contains an allocated string, line points to right after its nul byte.
struct history_entry {
  char *prefix;
  const char *line;
  bool is_new;
};

forward_dlist(_history_list, struct history_entry);
forward_hmap(_history_hmap, char *, _history_list_node *);

typedef _history_list_iter history_iter;

typedef struct History {
  _history_hmap map;
  _history_list list;
  _history_list_iter cur;
  size_t num_new_entries; // new entries to be written to the file (excluding
                          // ones with leading space)
} History;

void history_load(History *h, const char *path);
void history_write(History *h, const char *path, int histsize);
void history_append(History *h, const char *prefix, const char *line);
void history_reset(History *h);
void history_deinit(History *h);
const char *history_next_entry(History *h);
const char *history_prev(History *h);
size_t history_size(History *h);
history_iter history_begin(History *h);
void history_next(history_iter *it);
