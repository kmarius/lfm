#pragma once

/*
 * Minimal hashtable and linked hashtable with macros to iterate over the
 * contents. The buckets of the linked version form a linked list so that one
 * can iterate in insertion order.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HT_DEFAULT_CAPACITY 128

typedef void (*ht_free_fun)(void *);

struct hashtab_s;
struct linked_hashtab_s;

struct hashtab_s *ht_with_capacity(size_t capacity, ht_free_fun free);
static inline struct hashtab_s *ht_create(ht_free_fun free)
{
  return ht_with_capacity(HT_DEFAULT_CAPACITY, free);
}
void ht_destroy(struct hashtab_s *t);
void ht_set(struct hashtab_s *t, const char *key, void *val);
// create a copy of key/val and inserts it into the table
static inline void ht_set_copy(struct hashtab_s *t, const char *key, const void *val, size_t sz)
{
  char *mem = malloc(sz + strlen(key) + 1);
  memcpy(mem, val, sz);
  strcpy(mem + sz, key);
  ht_set(t, mem+sz, mem);
}
bool ht_delete(struct hashtab_s *t, const char *key);
void *ht_get(struct hashtab_s *t, const char *key);
void ht_clear(struct hashtab_s *t);


struct linked_hashtab_s *lht_with_capacity(size_t capacity, ht_free_fun free);
static inline struct linked_hashtab_s *lht_create(ht_free_fun free)
{
  return lht_with_capacity(HT_DEFAULT_CAPACITY, free);
}
void lht_destroy(struct linked_hashtab_s *t);
// returns false on update
bool lht_set(struct linked_hashtab_s *t, const char *key, void *val);
// returns true on delete
bool lht_delete(struct linked_hashtab_s *t, const char *key);
void *lht_get(const struct linked_hashtab_s *t, const char *key);
void lht_clear(struct linked_hashtab_s *t);

struct ht_bucket {
  const char *key;
  void *val;
  struct ht_bucket *next;
};

typedef struct hashtab_s {
  struct ht_bucket *buckets;
  size_t capacity;  // size of the actual table, not counting overflow lists
  size_t min_capacity;
  size_t size;
  ht_free_fun free;
} Hashtab;

struct lht_bucket {
  const char *key;
  void *val;
  struct lht_bucket *next; // next in overflow list
  struct lht_bucket *order_next; // next/prev in the element ordering
  struct lht_bucket *order_prev;
};

typedef struct linked_hashtab_s {
  struct lht_bucket *buckets;
  size_t capacity;
  size_t min_capacity;
  size_t size;
  struct lht_bucket *first;
  struct lht_bucket *last;
  ht_free_fun free;
} LinkedHashtab;

//
// public macros
//

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

#define lht_foreach(item, t) \
  for (struct lht_bucket *lht_cont, *lht_b = (t)->first; \
      (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next) \
      for (item = lht_b->val; lht_cont; lht_cont = NULL)

#define lht_foreach_kv(k, v, t) \
  for (struct lht_bucket *lht_cont, *lht_b = (t)->first; \
      (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next) \
      for (k = lht_b->key; lht_cont; ) \
      for (v = lht_b->val; lht_cont; lht_cont = NULL)
