/*
 * Combines a doubly linked list with a hash map to retain insertion order and
 * allow fast lookups. Used for file selection/copy buffer.
 */

#pragma once

#include "stc/types.h"

#include <stdbool.h>

declare_dlist(_pathlist_list, char *);
declare_hmap(_pathlist_hmap, const char *, _pathlist_list_node *);

typedef struct pathlist {
  _pathlist_hmap map;
  _pathlist_list list;
} pathlist;

typedef _pathlist_list_iter pathlist_iter;

void pathlist_init(pathlist *self);
void pathlist_deinit(pathlist *self);
bool pathlist_contains(const pathlist *self, const char *path);
void pathlist_add(pathlist *self, const char *path);
bool pathlist_remove(pathlist *self, const char *path);
void pathlist_clear(pathlist *self);
size_t pathlist_size(const pathlist *self);
pathlist_iter pathlist_begin(const pathlist *self);
void pathlist_next(pathlist_iter *it);
