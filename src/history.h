#pragma once

#include "stc/forward.h"

#include <stdbool.h>
#include <stddef.h>

struct history_entry {
  char *prefix;     // .prefix contains an allocated string including the actual
                    // line
  const char *line; // points to right after the first nul byte of .prefix
  bool is_new;      // true if this item is new and not previously read from the
                    // history file
};

forward_dlist(_history_list, struct history_entry);
forward_hmap(_history_hmap, const char *, _history_list_node *);
typedef _history_list_iter history_iter;

typedef struct History {
  _history_hmap map;      // maps lines to list nodes in .list
  _history_list list;     // `history_entry` in a doubly linked list
  _history_list_iter cur; // points to the current history item, manipulated by
                          // history_prev, history_next, history_reset
  size_t num_new_entries; // new entries to be written to the file (excluding
                          // ones with leading space)
} History;

/*
 * Initialize a history object and load history from file `path`.
 */
void history_load(History *h, const char *path);

/*
 * Write history to file `path`.
 */
void history_write(History *h, const char *path, int histsize);

/*
 * Append a line to the history. Duplicates are eliminated and only the newest
 * item is kept. Invalidates all iterators.
 */
void history_append(History *h, const char *prefix, const char *line);

/*
 * Reset the `cur` pointer into the history.
 */
void history_reset(History *h);

/*
 * Deinitialize a history object.
 */
void history_deinit(History *h);

/*
 * Get the next history item relative to the `cur` pointer and increment it.
 */
const char *history_next_entry(History *h);

/*
 * Get the previous history item relative to the `cur` pointer and decrement it.
 */
const char *history_prev(History *h);

/*
 * Get the number of lines in the history object.
 */
size_t history_size(History *h);

/*
 * Create an iterator for the history object.
 */
history_iter history_begin(History *h);

/*
 * Advance an iterator for the history object.
 */
void history_next(history_iter *it);
