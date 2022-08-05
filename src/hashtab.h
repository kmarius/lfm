#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// Minimal hash table to be used in directory/preview caches.

typedef void (*free_fun)(void *);

struct ht_bucket {
  const char *key;
  void *val;
  struct ht_bucket *next;
};

typedef struct Hashtab {
  struct ht_bucket *buckets;
  size_t size;
  size_t capacity;  // size of the actual table, not counting overflow lists
  size_t min_capacity;
  free_fun free;
  uint8_t version;
} Hashtab;

Hashtab *ht_init(Hashtab *t, size_t capacity, free_fun free);
Hashtab *ht_deinit(Hashtab *t);
static inline Hashtab *ht_create(size_t capacity, free_fun free)
{
  return ht_init(malloc(sizeof(Hashtab)), capacity, free);
}
static inline void ht_destroy(Hashtab *t)
{
  free(ht_deinit(t));
}
void ht_set(Hashtab *t, const char *key, void *val);
void *ht_get(Hashtab *t, const char *key);
void ht_clear(Hashtab *t);

// iterate over values
#define ht_foreach(item, h) \
  for (size_t ht_i = 0; ht_i < (h)->capacity; ht_i++) \
  for (struct ht_bucket *ht_cont, *ht_b = &(h)->buckets[ht_i]; \
      (ht_cont = ht_b) && ht_b->val; ht_b = ht_b->next) \
  for (item = ht_b->val; ht_cont; ht_cont = NULL)

#define ht_foreach_kv(k, v, h) \
  for (size_t ht_i = 0; ht_i < (h)->capacity; ht_i++) \
  for (struct ht_bucket *ht_cont, *ht_b = &(h)->buckets[ht_i]; \
      (ht_cont = ht_b) && ht_b->val; ht_b = ht_b->next) \
  for (k = ht_b->key; ht_cont; ) \
  for (v = ht_b->val; ht_cont; ht_cont = NULL)
