#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "linkedhashtab.h"
#include "util.h"

#define T LinkedHashtab

// https://en.m.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static uint64_t hash(const char *s)
{
  uint64_t h = 0xcbf29ce484222325;
  while (*s) {
    h = (h ^ *s++) * 0x00000100000001B3;
  }
  return h;
}


T *lht_init(T *t, size_t size, free_fun free)
{
  t->nbuckets = size;
  t->buckets = calloc(t->nbuckets, sizeof *t->buckets);
  t->size = 0;
  t->free = free;
  t->first = NULL;
  t->last = NULL;
  return t;
}


T *lht_deinit(T *t)
{
  for (size_t i = 0; i < t->nbuckets; i++) {
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


// Returns true if successfull and the corresponding bucket in b: Otherwise,
// b contains the bucket (head of the list) if empty, or the element to which
// the new node should be appended to (check with (*b)->val).
static bool probe(T *t, const char *key, struct lht_bucket **b)
{
  *b = &t->buckets[hash(key) % t->nbuckets];
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
    *b = bb->next;
  }
}


// update keeps the order
bool lht_set(T *t, const char *key, void *val)
{
  bool ret = true;
  struct lht_bucket *b;
  if (!probe(t, key, &b)) {
    if (b->val) {
      b->next = calloc(1, sizeof *b->next);
      b->next->prev = b;
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
    t->size++;
  } else if (b->val && t->free) {
    ret = false;
    t->free(b->val);
  }
  b->key = key;
  b->val = val;
  return ret;
}

bool lht_delete(T *t, const char *key)
{
  struct lht_bucket *b;
  if (probe(t, key, &b) && b->val) {
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
    if (b < t->buckets || b >= t->buckets + t->nbuckets) {
      // overflow bucket
      if (b->order_prev) {
        b->order_prev->order_next = b->order_next;
      }
      if (b->order_next) {
        b->order_next->order_prev = b->order_prev;
      }
      if (b->next) {
        b->next->prev = b->prev;
      }
      if (b->prev) {
        b->prev->next = b->next;
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
        if (next->next) {
          next->next->prev = b;
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
    return true;
  }
  return false;
}


void *lht_get(T *t, const char *key)
{
  struct lht_bucket *b;
  if (probe(t, key, &b)) {
    return b->val;
  }
  return NULL;
}


void lht_clear(T *t)
{
  for (size_t i = 0; i < t->nbuckets; i++) {
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
  t->first = NULL;
  t->last = NULL;
  t->size = 0;
}
