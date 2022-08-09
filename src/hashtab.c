#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hashtab.h"
#include "util.h"

#define T Hashtab

#define GROW_THRESHOLD 0.75
#define SHRINK_THRESHOLD 0.125

// https://en.m.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static uint64_t hash(const char *s)
{
  uint64_t h = 0xcbf29ce484222325;
  while (*s) {
    h = (h ^ *s++) * 0x00000100000001B3;
  }
  return h;
}


T *ht_init(T *t, size_t capacity, free_fun free)
{
  memset(t, 0, sizeof *t);
  t->capacity = capacity;
  t->min_capacity = capacity;
  t->buckets = calloc(t->capacity, sizeof *t->buckets);
  t->free = free;
  return t;
}


T *ht_deinit(T *t)
{
  for (size_t i = 0; i < t->capacity; i++) {
    if (t->buckets[i].val) {
      for (struct ht_bucket *next, *b = t->buckets[i].next; b; b = next) {
        next = b->next;
        if (t->free) {
          t->free(b->val);
        }
        free(b);
      }
      if (t->free) {
        t->free(t->buckets[i].val);
      }
    }
  }
  free(t->buckets);
  return t;
}


static inline void ht_resize(T *t, size_t capacity)
{
  // log_debug("resizing from %lu to %lu", t->capacity, capacity);
  T *new = ht_create(capacity, t->free);
  new->min_capacity = t->min_capacity;
  ht_foreach_kv(const char *k, void *v, t) {
    ht_set(new, k, v);
  }
  t->free = NULL;
  ht_deinit(t);
  memcpy(t, new, sizeof *t);
  free(new);
}


static inline void ht_grow(T *t)
{
  ht_resize(t, t->capacity * 2);
}


// // we are not deleting single values, currenctly
// static inline void ht_shrink(T *t)
// {
//   if (t->capacity/2 >= t->min_capacity) {
//     ht_resize(t, t->capacity/2);
//   }
// }


// Returns true if successfull and the corresponding bucket in b: Otherwise,
// b contains the bucket (head of the list) if empty, or the element to which
// the new node should be appended to (check with (*b)->val).
static bool probe(T *t, const char *key, struct ht_bucket **b)
{
  *b = &t->buckets[hash(key) % t->capacity];
  for (;;) {
    struct ht_bucket *bb = *b;
    if (!bb->key) {
      return false;
    }
    if (streq(bb->key, key)) {
      return true;
    }
    if (!bb->next) {
      return false;
    }
    *b = bb->next;
  }
}


void ht_set(T *t, const char *key, void *val)
{
  struct ht_bucket *b;
  if (!probe(t, key, &b) && b->val) {
    b->next = malloc(sizeof *b->next);
    b = b->next;
    b->next = NULL;
  } else if (b->val && t->free) {
    t->free(b->val);
  }
  if (!b->val) {
    t->size++;
  }
  b->key = key;
  b->val = val;
  if (t->size > GROW_THRESHOLD * t->capacity) {
    ht_grow(t);
  }
}


void *ht_get(T *t, const char *key)
{
  struct ht_bucket *b;
  if (probe(t, key, &b)) {
    return b->val;
  }
  return NULL;
}


void ht_clear(T *t)
{
  for (size_t i = 0; i < t->capacity; i++) {
    if (t->buckets[i].val) {
      for (struct ht_bucket *next, *b = t->buckets[i].next; b; b = next) {
        next = b->next;
        if (t->free) {
          t->free(b->val);
        }
        free(b);
      }
      if (t->free) {
        t->free(t->buckets[i].val);
      }
      t->buckets[i].val = NULL;
      t->buckets[i].key = NULL;
      t->buckets[i].next = NULL;
    }
  }
  ht_resize(t, t->min_capacity);
  t->version++;
}
