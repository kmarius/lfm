#include "pathlist.h"

#include <assert.h>

#define i_declared
#define i_type _pathlist_list
#define i_keypro cstr
#include "stc/dlist.h"

// we don't use keypro here because keys are owned by the list
// using i_keypro cstr makes it impossible to look up cstr, only const char*
// work
#define i_declared
#define i_type _pathlist_hmap
#define i_key cstr
#define i_val _pathlist_list_node *
#define i_eq cstr_eq
#define i_hash cstr_hash
#include "stc/hmap.h"

void pathlist_init(pathlist *self) {
  self->map = _pathlist_hmap_init();
  self->list = _pathlist_list_init();
}

void pathlist_drop(pathlist *self) {
  _pathlist_list_drop(&self->list);
  _pathlist_hmap_drop(&self->map);
}

void pathlist_clear(pathlist *self) {
  _pathlist_list_clear(&self->list);
  _pathlist_hmap_clear(&self->map);
}

bool pathlist_contains(const pathlist *self, const cstr *path) {
  return _pathlist_hmap_contains(&self->map, *path);
}

void pathlist_add(pathlist *self, const cstr *path) {
  if (!pathlist_contains(self, path)) {
    cstr *val = _pathlist_list_push_back(&self->list, cstr_clone(*path));
    _pathlist_hmap_insert(&self->map, *val, _pathlist_list_get_node(val));
  }
}

bool pathlist_remove(pathlist *self, const cstr *path) {
  const _pathlist_hmap_iter val = _pathlist_hmap_find(&self->map, *path);
  if (val.ref) {
    _pathlist_list_erase_node(&self->list, val.ref->second);
    _pathlist_hmap_erase_at(&self->map, val);
  }
  return val.ref != NULL;
}

size_t pathlist_size(const pathlist *self) {
  return _pathlist_hmap_size(&self->map);
}

pathlist_iter pathlist_begin(const pathlist *self) {
  return _pathlist_list_begin(&self->list);
}

void pathlist_next(pathlist_iter *it) {
  _pathlist_list_next(it);
}
