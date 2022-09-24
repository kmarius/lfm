#include <stdio.h>
#include <stdlib.h>

#include "cvector.h"
#include "trie.h"

static inline Trie *trie_node_create(input_t key, Trie *next)
{
  Trie *n = malloc(sizeof *n);
  n->key = key;
  n->keys = NULL;
  n->desc = NULL;
  n->child = NULL;
  n->next = next;
  return n;
}


Trie *trie_create()
{
  return trie_node_create(0, NULL);
}


Trie *trie_find_child(const Trie* t, input_t key)
{
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


Trie *trie_insert(Trie* t, const input_t *trie_keys, const char *keys, const char *desc)
{
  if (!t) {
    return NULL;
  }

  for (const input_t *c = trie_keys; *c != 0; c++) {
    Trie *n = trie_find_child(t, *c);
    if (!n) {
      n = trie_node_create(*c, t->child);
      t->child = n;
    }
    t = n;
  }
  free(t->desc);
  free(t->keys);
  t->desc = desc ? strdup(desc) : NULL;
  t->keys = keys ? strdup(keys) : NULL;
  return t;
}


Trie *trie_remove(Trie* t, const input_t *trie_keys)
{
  if (*trie_keys == 0) {
    free(t->keys);
    free(t->desc);
    t->keys = NULL;
    t->desc = NULL;
    return t;
  }

  Trie **prev = &t->child;
  for (Trie *n = t->child; n; n = n->next) {
    if (n->key == *trie_keys) {
      Trie *ret = trie_remove(n, trie_keys + 1);
      if (!n->child && !n->keys) {
        *prev = n->next;
        free(n);
      }
      return ret;
    }
    prev = &n->next;
  }
  return NULL;
}


void trie_collect_leaves(Trie *t, cvector_vector_type(Trie *) *vec, bool prune)
{
  if (!t) {
    return;
  }

  if (t->keys) {
    cvector_push_back(*vec, t);
    if (prune) {
      return;
    }
  }

  for (Trie *n = t->child; n; n = n->next) {
    trie_collect_leaves(n, vec, prune);
  }
}


void trie_destroy(Trie *t)
{
  if (!t) {
    return;
  }

  for (Trie* next, *n = t->child; n; n = next) {
    next = n->next;
    trie_destroy(n);
  }

  free(t->desc);
  free(t->keys);
  free(t);
}
