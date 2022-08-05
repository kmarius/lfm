#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*free_fun)(void *);

struct lht_bucket {
  const char *key;
  void *val;
  struct lht_bucket *next; // next in overflow list
  struct lht_bucket *order_next; // next/prev in the element ordering
  struct lht_bucket *order_prev;
};

typedef struct LinkedHashtab {
  struct lht_bucket *buckets;
  uint32_t capacity;
  uint32_t min_capacity;
  uint32_t size;
  struct lht_bucket *first;
  struct lht_bucket *last;
  free_fun free;
} LinkedHashtab;

LinkedHashtab *lht_init(LinkedHashtab *t, size_t size, free_fun free);
LinkedHashtab *lht_deinit(LinkedHashtab *t);
static inline LinkedHashtab *lht_create(size_t size, free_fun free)
{
  return lht_init(malloc(sizeof(LinkedHashtab)), size, free);
}
static inline void lht_destroy(LinkedHashtab *t)
{
  free(lht_deinit(t));
}
// returns false on update
bool lht_set(LinkedHashtab *t, const char *key, void *val);
// returns true on delete
bool lht_delete(LinkedHashtab *t, const char *key);
void *lht_get(const LinkedHashtab *t, const char *key);
void lht_clear(LinkedHashtab *t);

// iterate over values
#define lht_foreach(item, t) \
  for (struct lht_bucket *lht_cont, *lht_b = (t)->first; \
      (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next) \
  for (item = lht_b->val; lht_cont; lht_cont = NULL)

#define lht_foreach_kv(k, v, t) \
  for (struct lht_bucket *lht_cont, *lht_b = (t)->first; \
      (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next) \
  for (k = lht_b->key; lht_cont; ) \
  for (v = lht_b->val; lht_cont; lht_cont = NULL)
