#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hashtab.h"
#include "util.h"

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


static inline Hashtab *ht_init(Hashtab *ht, size_t capacity, ht_free_fun free)
{
  memset(ht, 0, sizeof *ht);
  ht->capacity = capacity;
  ht->min_capacity = capacity;
  ht->buckets = calloc(ht->capacity, sizeof *ht->buckets);
  ht->free = free;
  return ht;
}


static inline Hashtab *ht_deinit(Hashtab *ht)
{
  for (size_t i = 0; i < ht->capacity; i++) {
    if (ht->buckets[i].val) {
      for (struct ht_bucket *next, *b = ht->buckets[i].next; b; b = next) {
        next = b->next;
        if (ht->free) {
          ht->free(b->val);
        }
        free(b);
      }
      if (ht->free) {
        ht->free(ht->buckets[i].val);
      }
    }
  }
  free(ht->buckets);
  return ht;
}


Hashtab *ht_with_capacity(size_t capacity, ht_free_fun free)
{
  return ht_init(malloc(sizeof(Hashtab)), capacity, free);
}


void ht_destroy(Hashtab *ht)
{
  free(ht_deinit(ht));
}


static inline void ht_resize(Hashtab *ht, size_t capacity)
{
  // log_debug("resizing from %lu to %lu", ht->capacity, capacity);
  Hashtab *new = ht_with_capacity(capacity, ht->free);
  new->min_capacity = ht->min_capacity;
  ht_foreach_kv(const char *k, void *v, ht) {
    ht_set(new, k, v);
  }
  ht->free = NULL;
  ht_deinit(ht);
  memcpy(ht, new, sizeof *ht);
  free(new);
}


static inline void ht_grow(Hashtab *ht)
{
  ht_resize(ht, ht->capacity * 2);
}


static inline void ht_shrink(Hashtab *ht)
{
  if (ht->capacity/2 >= ht->min_capacity) {
    ht_resize(ht, ht->capacity/2);
  }
}


// Returns true if successfull and the corresponding bucket in b: Otherwise,
// b contains the bucket (head of the list) if empty, or the element to which
// the new node should be appended to (check with (*b)->val).
static bool ht_probe(Hashtab *ht, const char *key, struct ht_bucket **b, struct ht_bucket **prev)
{
  *b = &ht->buckets[hash(key) % ht->capacity];
  if (prev) {
    *prev = NULL;
  }
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
    if (prev) {
      *prev = bb;
    }
    *b = bb->next;
  }
}


void ht_set(Hashtab *ht, const char *key, void *val)
{
  struct ht_bucket *b;
  if (!ht_probe(ht, key, &b, NULL) && b->val) {
    b->next = malloc(sizeof *b->next);
    b = b->next;
    b->next = NULL;
  } else if (b->val && ht->free) {
    ht->free(b->val);
  }
  if (!b->val) {
    ht->size++;
  }
  b->key = key;
  b->val = val;
  if (ht->size > GROW_THRESHOLD * ht->capacity) {
    ht_grow(ht);
  }
}


void *ht_get(Hashtab *ht, const char *key)
{
  struct ht_bucket *b;
  if (ht_probe(ht, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}


bool ht_delete(Hashtab *ht, const char *key)
{
  struct ht_bucket *b, *prev;
  if (ht_probe(ht, key, &b, &prev) && b->val) {
    ht->size--;
    if (ht->free) {
      ht->free(b->val);
    }
    if (prev) {
      // overflow bucket
      prev->next = b->next;
      free(b);
    } else {
      // array bucket
      if (b->next) {
        // move overflow bucket into array
        struct ht_bucket *next = b->next;
        memcpy(b, next, sizeof *b);
        free(next);
      } else {
        b->val = NULL;
        b->key = NULL;
      }
    }
    if (ht->size < SHRINK_THRESHOLD * ht->capacity) {
      ht_shrink(ht);
    }
    return true;
  }
  return false;
}


void ht_clear(Hashtab *ht)
{
  for (size_t i = 0; i < ht->capacity; i++) {
    if (ht->buckets[i].val) {
      for (struct ht_bucket *next, *b = ht->buckets[i].next; b; b = next) {
        next = b->next;
        if (ht->free) {
          ht->free(b->val);
        }
        free(b);
      }
      if (ht->free) {
        ht->free(ht->buckets[i].val);
      }
      ht->buckets[i].val = NULL;
      ht->buckets[i].key = NULL;
      ht->buckets[i].next = NULL;
    }
  }
  ht_resize(ht, ht->min_capacity);
}


static inline LinkedHashtab *lht_init(LinkedHashtab *lht, size_t capacity, ht_free_fun free)
{
  memset(lht, 0, sizeof *lht);
  lht->capacity = capacity;
  lht->min_capacity = capacity;
  lht->buckets = calloc(lht->capacity, sizeof *lht->buckets);
  lht->free = free;
  return lht;
}


static inline LinkedHashtab *lht_deinit(LinkedHashtab *lht)
{
  for (size_t i = 0; i < lht->capacity; i++) {
    if (lht->buckets[i].val) {
      for (struct lht_bucket *next, *b = lht->buckets[i].next; b; b = next) {
        next = b->next;
        if (lht->free) {
          lht->free(b->val);
        }
        free(b);
      }
      if (lht->free) {
        lht->free(lht->buckets[i].val);
      }
    }
  }
  free(lht->buckets);
  return lht;
}


LinkedHashtab *lht_with_capacity(size_t capacity, ht_free_fun free)
{
  return lht_init(malloc(sizeof(LinkedHashtab)), capacity, free);
}


void lht_destroy(LinkedHashtab *lht)
{
  free(lht_deinit(lht));
}


// Returns true on successfull lookup, sets *b to the corresponding bucket.
// On fail, b contains either the empty bucket in the bucket array OR the
// nonempty bucket to which the next bucket should be appended to.
// (distinguish with (*b)->val != NULL) if prev is non NULL, it will be set
// to the previous bucket of the overflow list, which is needed for deletion.
static bool lht_probe(const LinkedHashtab *lht, const char *key, struct lht_bucket **b, struct lht_bucket **prev)
{
  *b = &lht->buckets[hash(key) % lht->capacity];
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


// it would be better to be able to give a size hint so we don'lht have to
// resize multiple times if we fill the table e.g. with a huge directory
// won'lht bother for now, because when does that even happen?
static inline void lht_resize(LinkedHashtab *lht, size_t capacity)
{
  // log_debug("resizing from %lu to %lu", lht->capacity, capacity);
  LinkedHashtab *new = lht_with_capacity(capacity, lht->free);
  new->min_capacity = lht->min_capacity;
  lht_foreach_kv(const char *k, void *v, lht) {
    lht_set(new, k, v);
  }
  lht->free = NULL;
  lht_deinit(lht);
  memcpy(lht, new, sizeof *lht);
  free(new);
}


static inline void lht_grow(LinkedHashtab *lht)
{
  lht_resize(lht, lht->capacity * 2);
}


static inline void lht_shrink(LinkedHashtab *lht)
{
  if (lht->capacity/2 >= lht->min_capacity) {
    lht_resize(lht, lht->capacity/2);
  }
}


// update keeps the order
bool lht_set(LinkedHashtab *lht, const char *key, void *val)
{
  bool ret = true;
  struct lht_bucket *b;
  if (!lht_probe(lht, key, &b, NULL)) {
    if (b->val) {
      b->next = calloc(1, sizeof *b->next);
      b = b->next;
    }
    b->next = NULL;
    b->order_next = NULL;
    if (lht->last) {
      lht->last->order_next = b;
      b->order_prev = lht->last;
    } else {
      b->order_prev = NULL;
    }
    lht->last = b;
    if (!lht->first) {
      lht->first = b;
    }
  } else if (b->val && lht->free) {
    ret = false;
    lht->free(b->val);
  }
  if (!b->val) {
    lht->size++;
  }
  b->key = key;
  b->val = val;
  if (lht->size > GROW_THRESHOLD * lht->capacity) {
    lht_grow(lht);
  }
  return ret;
}


bool lht_delete(LinkedHashtab *lht, const char *key)
{
  struct lht_bucket *b, *prev;
  if (lht_probe(lht, key, &b, &prev) && b->val) {
    lht->size--;
    if (lht->free) {
      lht->free(b->val);
    }
    if (lht->first == b) {
      lht->first = b->order_next;
    }
    if (lht->last == b) {
      lht->last = b->order_prev;
    }
    if (prev) {
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
        if (lht->first == next) {
          lht->first = b;
        }
        if (lht->last == next) {
          lht->last = b;
        }
        memcpy(b, next, sizeof *b);
        free(next);
      } else {
        b->val = NULL;
        b->key = NULL;
      }
    }
    if (lht->size < SHRINK_THRESHOLD * lht->capacity) {
      lht_shrink(lht);
    }
    return true;
  }
  return false;
}


void *lht_get(const LinkedHashtab *lht, const char *key)
{
  struct lht_bucket *b;
  if (lht_probe(lht, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}


void lht_clear(LinkedHashtab *lht)
{
  for (size_t i = 0; i < lht->capacity; i++) {
    if (lht->buckets[i].val) {
      for (struct lht_bucket *next, *b = lht->buckets[i].next; b; b = next) {
        next = b->next;
        if (lht->free) {
          lht->free(b->val);
        }
        free(b);
      }
      if (lht->free) {
        lht->free(lht->buckets[i].val);
      }
      memset(&lht->buckets[i], 0, sizeof(struct lht_bucket));
    }
  }
  lht_resize(lht, lht->min_capacity);
  lht->first = NULL;
  lht->last = NULL;
}
