#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "hashtab.h"
#include "util.h"

#define T Hashtab

// https://en.m.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static uint64_t hash(const char *s)
{
  uint64_t h = 0xcbf29ce484222325;
  while (*s) {
    h = (h ^ *s++) * 0x00000100000001B3;
  }
  return h;
}


T *hashtab_init(T *t, size_t size, free_fun free)
{
  t->nbuckets = size;
  t->buckets = calloc(t->nbuckets, sizeof *t->buckets);
  t->free = free;
  t->version = 0;
  return t;
}


void hashtab_deinit(T *t)
{
  for (size_t i = 0; i < t->nbuckets; i++) {
    if (t->buckets[i].val) {
      for (struct bucket *next, *b = t->buckets[i].next; b; b = next) {
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
}


// Returns true if successfull and the corresponding bucket in b: Otherwise,
// b contains the bucket (head of the list) if empty, or the element to which
// the new node should be appended to (check with (*b)->val).
static bool probe(T *t, const char *key, struct bucket **b)
{
  *b = &t->buckets[hash(key) % t->nbuckets];
  for (;;) {
    struct bucket *bb = *b;
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


void hashtab_set(T *t, const char *key, void *val)
{
  struct bucket *b;
  if (!probe(t, key, &b) && b->val) {
    b->next = malloc(sizeof *b->next);
    b = b->next;
    b->next = NULL;
  } else if (b->val && t->free) {
    t->free(b->val);
  }
  b->key = key;
  b->val = val;
}


void *hashtab_get(T *t, const char *key)
{
  struct bucket *b;
  if (probe(t, key, &b)) {
    return b->val;
  }
  return NULL;
}


void hashtab_clear(T *t)
{
  for (size_t i = 0; i < t->nbuckets; i++) {
    if (t->buckets[i].val) {
      for (struct bucket *next, *b = t->buckets[i].next; b; b = next) {
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
  t->version++;
}

struct ht_stats hashtab_stats(T *t)
{
  struct ht_stats stats = { .nbuckets = t->nbuckets, };

  for (size_t i = 0; i < t->nbuckets; i++) {
    if (t->buckets[i].val) {
      uint16_t size = 1;
      stats.buckets_nonempty++;
      stats.nelems++;
      for (struct bucket *b = t->buckets[i].next; b; b = b->next) {
        stats.nelems++;
        size++;
      }
      if (size > stats.bucket_size_max) {
        stats.bucket_size_max = size;
      }
    }
  }

  stats.alpha = 1.0 * stats.nelems / t->nbuckets;
  stats.bucket_nonempty_avg_size = (1.0 * stats.buckets_nonempty) / stats.nelems;
  return stats;
}
