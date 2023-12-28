#include "pathlist.h"

#include <assert.h>

#define i_is_forward
#define i_type _pathlist_list
#define i_key char *
#define i_keyraw const char *
#define i_keyto(p) (*p)
#define i_keyfrom(p) strdup(p)
#define i_keyclone(p) strdup(p)
#define i_keydrop(p) free(*(p))
#define i_eq(p, q) ((*p) == (*q))
#define i_noclone
#include "stc/list.h"

#define i_is_forward
#define i_type _pathlist_hmap
#define i_key const char *
#define i_val _pathlist_list_node *
#define i_hash ccharptr_hash
#define i_eq(p, q) (!strcmp(*(p), *(q)))
#include "stc/hmap.h"

void pathlist_init(pathlist *self) {
  self->map = _pathlist_hmap_init();
  self->list = _pathlist_list_init();
}

void pathlist_deinit(pathlist *self) {
  _pathlist_list_drop(&self->list);
  _pathlist_hmap_drop(&self->map);
}

void pathlist_clear(pathlist *self) {
  _pathlist_list_clear(&self->list);
  _pathlist_hmap_clear(&self->map);
}

bool pathlist_contains(const pathlist *self, const char *path) {
  return _pathlist_hmap_contains(&self->map, path);
}

void pathlist_add(pathlist *self, const char *path) {
  if (!pathlist_contains(self, path)) {
    char **val = _pathlist_list_emplace_back(&self->list, path);
    _pathlist_hmap_insert(&self->map, *val, _pathlist_list_get_node(val));
  }
}

bool pathlist_remove(pathlist *self, const char *path) {
  const _pathlist_hmap_iter val = _pathlist_hmap_find(&self->map, path);
  if (val.ref) {
    assert(_pathlist_list_remove(&self->list, val.ref->second->value));
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
