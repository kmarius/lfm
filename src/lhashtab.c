#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hashtab.h"
#include "util.h"

#define T LinkedHashtab

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


T *lht_init(T *t, size_t capacity, free_fun free)
{
  memset(t, 0, sizeof *t);
  t->capacity = capacity;
  t->min_capacity = capacity;
  t->buckets = calloc(t->capacity, sizeof *t->buckets);
  t->free = free;
  return t;
}


T *lht_deinit(T *t)
{
  for (size_t i = 0; i < t->capacity; i++) {
    if (t->buckets[i].val) {
      for (struct lht_bucket *next, *b = t->buckets[i].next; b; b = next) {
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


// Returns true on successfull lookup, sets *b to the corresponding bucket.
// On fail, b contains either the empty bucket in the bucket array OR the
// nonempty bucket to which the next bucket should be appended to.
// (distinguish with (*b)->val != NULL) if prev is non NULL, it will be set
// to the previous bucket of the overflow list, which is needed for deletion.
static bool probe(const T *t, const char *key, struct lht_bucket **b, struct lht_bucket **prev)
{
  *b = &t->buckets[hash(key) % t->capacity];
  if (prev) {
    *prev = NULL;
  }
  for (;;) {
    struct lht_bucket *bb = *b;
    if (!bb->key) {
      return false;
    }
    if (streq(bb->key, key)) {
      return true;
    }
    if (!bb->next) {
      return false;
    }
    if (prev) {
      *prev = bb;
    }
    *b = bb->next;
  }
}


// it would be better to be able to give a size hint so we don't have to
// resize multiple times if we fill the table e.g. with a huge directory
// won't bother for now, because when does that even happen?
static inline void lht_resize(T *t, size_t capacity)
{
  // log_debug("resizing from %lu to %lu", t->capacity, capacity);
  T *new = lht_create(capacity, t->free);
  new->min_capacity = t->min_capacity;
  lht_foreach_kv(const char *k, void *v, t) {
    lht_set(new, k, v);
  }
  t->free = NULL;
  lht_deinit(t);
  memcpy(t, new, sizeof *t);
  free(new);
}


static inline void lht_grow(T *t)
{
  lht_resize(t, t->capacity * 2);
}


static inline void lht_shrink(T *t)
{
  if (t->capacity/2 >= t->min_capacity) {
    lht_resize(t, t->capacity/2);
  }
}


// update keeps the order
bool lht_set(T *t, const char *key, void *val)
{
  bool ret = true;
  struct lht_bucket *b;
  if (!probe(t, key, &b, NULL)) {
    if (b->val) {
      b->next = calloc(1, sizeof *b->next);
      b = b->next;
    }
    b->next = NULL;
    b->order_next = NULL;
    if (t->last) {
      t->last->order_next = b;
      b->order_prev = t->last;
    } else {
      b->order_prev = NULL;
    }
    t->last = b;
    if (!t->first) {
      t->first = b;
    }
  } else if (b->val && t->free) {
    ret = false;
    t->free(b->val);
  }
  if (!b->val) {
    t->size++;
  }
  b->key = key;
  b->val = val;
  if (t->size > GROW_THRESHOLD * t->capacity) {
    lht_grow(t);
  }
  return ret;
}


bool lht_delete(T *t, const char *key)
{
  struct lht_bucket *b, *prev;
  if (probe(t, key, &b, &prev) && b->val) {
    t->size--;
    if (t->free) {
      t->free(b->val);
    }
    if (t->first == b) {
      t->first = b->order_next;
    }
    if (t->last == b) {
      t->last = b->order_prev;
    }
    if (b < t->buckets || b >= t->buckets + t->capacity) {
      // overflow bucket
      if (b->order_prev) {
        b->order_prev->order_next = b->order_next;
      }
      if (b->order_next) {
        b->order_next->order_prev = b->order_prev;
      }
      if (prev) {
        prev->next = b->next;
      }
      free(b);
    } else {
      // array bucket
      if (b->next) {
        // move overflow bucket into array
        struct lht_bucket *next = b->next;
        if (next->order_prev) {
          next->order_prev->order_next = b;
        }
        if (next->order_next) {
          next->order_next->order_prev = b;
        }
        if (t->first == next) {
          t->first = b;
        }
        if (t->last == next) {
          t->last = b;
        }
        memcpy(b, next, sizeof *b);
        free(next);
      } else {
        b->val = NULL;
        b->key = NULL;
      }
    }
    if (t->size < SHRINK_THRESHOLD * t->capacity) {
      lht_shrink(t);
    }
    return true;
  }
  return false;
}


void *lht_get(const T *t, const char *key)
{
  struct lht_bucket *b;
  if (probe(t, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}


void lht_clear(T *t)
{
  for (size_t i = 0; i < t->capacity; i++) {
    if (t->buckets[i].val) {
      for (struct lht_bucket *next, *b = t->buckets[i].next; b; b = next) {
        next = b->next;
        if (t->free) {
          t->free(b->val);
        }
        free(b);
      }
      if (t->free) {
        t->free(t->buckets[i].val);
      }
      memset(&t->buckets[i], 0, sizeof(struct lht_bucket));
    }
  }
  lht_resize(t, t->min_capacity);
  t->first = NULL;
  t->last = NULL;
}
