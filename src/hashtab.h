#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// minimal hashtable and linked hash table

typedef void (*free_fun)(void *);

struct ht_bucket {
  const char *key;
  void *val;
  struct ht_bucket *next;
};

typedef struct Hashtab {
  struct ht_bucket *buckets;
  size_t capacity;  // size of the actual table, not counting overflow lists
  size_t min_capacity;
  size_t size;
  uint32_t version;
  free_fun free;
} Hashtab;

struct lht_bucket {
  const char *key;
  void *val;
  struct lht_bucket *next; // next in overflow list
  struct lht_bucket *order_next; // next/prev in the element ordering
  struct lht_bucket *order_prev;
};

typedef struct LinkedHashtab {
  struct lht_bucket *buckets;
  size_t capacity;
  size_t min_capacity;
  size_t size;
  struct lht_bucket *first;
  struct lht_bucket *last;
  free_fun free;
} LinkedHashtab;


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


LinkedHashtab *lht_init(LinkedHashtab *t, size_t capacity, free_fun free);
LinkedHashtab *lht_deinit(LinkedHashtab *t);
static inline LinkedHashtab *lht_create(size_t capacity, free_fun free)
{
  return lht_init(malloc(sizeof(LinkedHashtab)), capacity, free);
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
