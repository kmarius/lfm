#include "trie.h"

#include "memory.h"
#include "stcutil.h"

static inline Trie *trie_node_create(input_t key, Trie *next) {
  Trie *n = xcalloc(1, sizeof *n);
  n->key = key;
  n->next = next;
  return n;
}

Trie *trie_create(void) {
  return trie_node_create(0, NULL);
}

Trie *trie_find_child(const Trie *t, input_t key) {
  if (!t) {
    return NULL;
  }
  for (Trie *n = t->child; n; n = n->next) {
    if (n->key == key) {
      return n;
    }
  }
  return NULL;
}

int trie_insert(Trie *t, const input_t *trie_keys, int ref, zsview keys,
                zsview desc) {
  if (!t) {
    return 0;
  }
  for (const input_t *c = trie_keys; *c != 0; c++) {
    Trie *n = trie_find_child(t, *c);
    if (!n) {
      n = trie_node_create(*c, t->child);
      t->child = n;
    }
    t = n;
  }
  cstr_assign_zv(&t->keys, keys);
  cstr_assign_zv(&t->desc, desc);
  t->is_leaf = !cstr_is_empty(&t->keys);
  int oldref = t->ref;
  t->ref = ref;
  return oldref;
}

int trie_remove(Trie *t, const input_t *trie_keys) {
  if (!t) {
    return 0;
  }
  if (*trie_keys == 0) {
    cstr_clear(&t->keys);
    cstr_clear(&t->desc);
    int oldref = t->ref;
    t->ref = 0;
    return oldref;
  }
  Trie **prev = &t->child;
  for (Trie *n = t->child; n; n = n->next) {
    if (n->key == *trie_keys) {
      int ret = trie_remove(n, trie_keys + 1);
      if (!n->child && !n->ref) {
        *prev = n->next;
        xfree(n);
      }
      return ret;
    }
    prev = &n->next;
  }
  return 0;
}

static inline void trie_collect_leaves_impl(Trie *t, vec_trie *vec,
                                            bool prune) {
  if (!t) {
    return;
  }
  if (t->ref) {
    vec_trie_push(vec, t);
    if (prune) {
      return;
    }
  }
  for (Trie *n = t->child; n; n = n->next) {
    trie_collect_leaves_impl(n, vec, prune);
  }
}

vec_trie trie_collect_leaves(Trie *trie, bool prune) {
  vec_trie vec = {0};
  trie_collect_leaves_impl(trie, &vec, prune);
  return vec;
}

void trie_destroy(Trie *t) {
  if (!t) {
    return;
  }
  for (Trie *next, *n = t->child; n; n = next) {
    next = n->next;
    trie_destroy(n);
  }
  cstr_drop(&t->desc);
  cstr_drop(&t->keys);
  xfree(t);
}
