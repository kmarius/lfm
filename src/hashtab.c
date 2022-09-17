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


static inline T *ht_init(T *t, size_t capacity, ht_free_fun free)
{
  memset(t, 0, sizeof *t);
  t->capacity = capacity;
  t->min_capacity = capacity;
  t->buckets = calloc(t->capacity, sizeof *t->buckets);
  t->free = free;
  return t;
}


static inline T *ht_deinit(T *t)
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


T *ht_with_capacity(size_t capacity, ht_free_fun free)
{
  return ht_init(malloc(sizeof(T)), capacity, free);
}


void ht_destroy(T *t)
{
  free(ht_deinit(t));
}


static inline void ht_resize(T *t, size_t capacity)
{
  // log_debug("resizing from %lu to %lu", t->capacity, capacity);
  T *new = ht_with_capacity(capacity, t->free);
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
static bool ht_probe(T *t, const char *key, struct ht_bucket **b)
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
  if (!ht_probe(t, key, &b) && b->val) {
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
  if (ht_probe(t, key, &b)) {
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
}

#undef T
#define T LinkedHashtab

static inline T *lht_init(T *t, size_t capacity, ht_free_fun free)
{
  memset(t, 0, sizeof *t);
  t->capacity = capacity;
  t->min_capacity = capacity;
  t->buckets = calloc(t->capacity, sizeof *t->buckets);
  t->free = free;
  return t;
}


static inline T *lht_deinit(T *t)
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


T *lht_with_capacity(size_t capacity, ht_free_fun free)
{
  return lht_init(malloc(sizeof(T)), capacity, free);
}


void lht_destroy(T *t)
{
  free(lht_deinit(t));
}


// Returns true on successfull lookup, sets *b to the corresponding bucket.
// On fail, b contains either the empty bucket in the bucket array OR the
// nonempty bucket to which the next bucket should be appended to.
// (distinguish with (*b)->val != NULL) if prev is non NULL, it will be set
// to the previous bucket of the overflow list, which is needed for deletion.
static bool lht_probe(const T *t, const char *key, struct lht_bucket **b, struct lht_bucket **prev)
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
  T *new = lht_with_capacity(capacity, t->free);
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
  if (!lht_probe(t, key, &b, NULL)) {
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
  if (lht_probe(t, key, &b, &prev) && b->val) {
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
  if (lht_probe(t, key, &b, NULL)) {
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
