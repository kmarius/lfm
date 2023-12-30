#pragma once

#include "stc/forward.h"

#include <stdbool.h>

forward_dlist(_pathlist_list, char *);
forward_hmap(_pathlist_hmap, char *, _pathlist_list_node *);

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
