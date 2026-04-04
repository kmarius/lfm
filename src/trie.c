#include "trie.h"

#include "defs.h"
#include "memory.h"
#include "stcutil.h"

static inline Trie *create_node(input_t key, Trie *next) {
  Trie *n = xcalloc(1, sizeof *n);
  n->key = key;
  n->next = next;
  return n;
}

Trie *trie_create(void) {
  return create_node(0, NULL);
}

// if not found, and pred_out is passed, a new node containing the key
// should be inserted at *pred_out
__lfm_nonnull(1)
static inline Trie *find_child_impl(Trie *t, input_t key, Trie ***pred_out) {
  Trie *res = NULL;
  Trie **pred = &t->child;
  for (Trie *n = t->child; n; pred = &n->next, n = n->next) {
    if (n->key <= key) {
      if (n->key == key)
        res = n;
      break;
    }
  }
  if (pred_out)
    *pred_out = pred;
  return res;
}

Trie *trie_find_child(Trie *t, input_t key) {
  return find_child_impl(t, key, NULL);
}

i32 trie_insert(Trie *t, const input_t *trie_keys, i32 ref, zsview keys,
                zsview desc) {
  for (const input_t *c = trie_keys; *c != 0; c++) {
    Trie **pre;
    Trie *n = find_child_impl(t, *c, &pre);
    if (!n) {
      // insert after predecessor
      n = create_node(*c, *pre);
      *pre = n;
    }
    t = n;
  }
  cstr_assign_zv(&t->keys, keys);
  cstr_assign_zv(&t->desc, desc);
  t->is_leaf = !cstr_is_empty(&t->keys);
  i32 oldref = t->ref;
  t->ref = ref;
  return oldref;
}

static inline i32 clear_node(Trie *t) {
  cstr_drop(&t->keys);
  cstr_drop(&t->desc);
  i32 ref = t->ref;
  t->ref = 0;
  return ref;
}

i32 trie_remove(Trie *t, const input_t *trie_keys) {
  if (*trie_keys == 0)
    return clear_node(t);

  Trie **pre;
  Trie *n = find_child_impl(t, *trie_keys, &pre);
  if (!n)
    return 0;

  i32 ref = trie_remove(n, trie_keys + 1);
  if (!n->child && !n->ref) {
    // no children, not a leaf, unlink
    *pre = (*pre)->next;
    xfree(n);
  }

  return ref;
}

void trie_destroy(Trie *t) {
  if (!t)
    return;

  for (Trie *next, *n = t->child; n; n = next) {
    next = n->next;
    trie_destroy(n);
  }
  clear_node(t);
  xfree(t);
}

__lfm_nonnull()
static inline void trie_collect_leaves_impl(Trie *t, vec_trie *vec,
                                            bool prune) {
  if (t->ref) {
    vec_trie_push(vec, t);
    if (prune)
      return;
  }
  for (Trie *n = t->child; n; n = n->next) {
    trie_collect_leaves_impl(n, vec, prune);
  }
}

vec_trie trie_collect_leaves(Trie *trie, bool prune) {
  vec_trie vec = vec_trie_init();
  trie_collect_leaves_impl(trie, &vec, prune);
  return vec;
}
