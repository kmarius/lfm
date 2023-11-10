#pragma once

/*
 * Hashtable and linked hashtable with macros to iterate over the contents. The
 * table grows/shrinks dynamically, amortized by rehashing one bucket at a time.
 * The buckets of the linked version form a linked list so that one can iterate
 * in insertion order.
 *
 * Currently only hashes zero terminated strings and cannot store NULL.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

#define HT_DEFAULT_CAPACITY 128
#define HT_GROW_THRESHOLD 0.75
#define HT_SHRINK_THRESHOLD 0.125

typedef void (*ht_free_func)(void *);

struct Hashtab;
struct LinkedHashtab;

struct Hashtab *ht_init(struct Hashtab *ht, size_t capacity, ht_free_func free);

struct Hashtab *ht_deinit(struct Hashtab *ht);

// Create a new hash table with base `capacity` and a `free` function.
struct Hashtab *ht_with_capacity(size_t capacity, ht_free_func free);

// Create a new hash table using a default base capacity of
// `HT_DEFAULT_CAPCCITY` and a `free` function..
static inline struct Hashtab *ht_create(ht_free_func free) {
  return ht_with_capacity(HT_DEFAULT_CAPACITY, free);
}

// Destroy a hash table, freeing all elements.
void ht_destroy(struct Hashtab *t) __attribute__((nonnull));

// Insert or update key-value pair. Returns `true` on insert, `false` on update.
bool ht_set(struct Hashtab *t, const char *key, void *val)
    __attribute__((nonnull));

// Create a copy of key/val and inserts it into the table. The hash table should
// have `xfree` as its free function.
__attribute__((nonnull)) static inline void
ht_set_copy(struct Hashtab *t, const char *key, const void *val, size_t sz) {
  char *mem = xmalloc(sz + strlen(key) + 1);
  memcpy(mem, val, sz);
  strcpy(mem + sz, key);
  ht_set(t, mem + sz, mem);
}

// Delete `key` from the hash table. Returns `true` on deletion, `false` if the
// key wasn't found.
bool ht_delete(struct Hashtab *t, const char *key) __attribute__((nonnull));

// Probe the has table for `key`. Returns the corresponding value on success,
// `NULL` otherwise.
void *ht_get(struct Hashtab *t, const char *key) __attribute__((nonnull));

// Clear all values from the hash table.
void ht_clear(struct Hashtab *t) __attribute__((nonnull));

struct LinkedHashtab *lht_init(struct LinkedHashtab *t, size_t capacity,
                               ht_free_func free) __attribute__((nonnull(1)));

struct LinkedHashtab *lht_deinit(struct LinkedHashtab *t)
    __attribute__((nonnull(1)));

// Create a new hash table with base `capacity` and a `free` function.
struct LinkedHashtab *lht_with_capacity(size_t capacity, ht_free_func free);

// Create a new hash table using a default base capacity of
// `HT_DEFAULT_CAPCCITY` and a `free` function..
static inline struct LinkedHashtab *lht_create(ht_free_func free) {
  return lht_with_capacity(HT_DEFAULT_CAPACITY, free);
}

// Destroy a hash table, freeing all elements.
void lht_destroy(struct LinkedHashtab *t) __attribute__((nonnull));

// Insert or update key-value pair. Returns `true` on insert, `false` on update.
bool lht_set(struct LinkedHashtab *t, const char *key, void *val)
    __attribute__((nonnull));

// returns true on delete
bool lht_delete(struct LinkedHashtab *t, const char *key)
    __attribute__((nonnull));

// Probe the has table for `key`. Returns the corresponding value on success,
// `NULL` otherwise.
void *lht_get(const struct LinkedHashtab *t, const char *key)
    __attribute__((nonnull));

// Clear all values from the hash table.
void lht_clear(struct LinkedHashtab *t) __attribute__((nonnull));

struct ht_bucket {
  const char *key;
  void *val;
  struct ht_bucket *next;
};

typedef struct Hashtab {
  struct ht_bucket *buckets;
  size_t capacity; // All buckets currently in use (the actual array might be
                   // larger)
  size_t n;        // base capacity
  size_t size;     // number of elements held
  size_t xptr;     // index of the next bucket to be rehashed
  uint8_t xlvl;    // number of full expansions completed
  ht_free_func free;
} Hashtab;

struct lht_bucket {
  const char *key;
  void *val;
  struct lht_bucket *next;       // next in overflow list
  struct lht_bucket *order_next; // next/prev in the insertion order
  struct lht_bucket *order_prev;
};

typedef struct LinkedHashtab {
  struct lht_bucket *buckets;
  size_t capacity;
  size_t n;
  size_t size;
  struct lht_bucket *first;
  struct lht_bucket *last;
  size_t xptr;
  uint8_t xlvl;
  ht_free_func free;
} LinkedHashtab;

//
// public macros
//

// Iterate over the values of the hash table in unspecified order.
#define ht_foreach(item, h)                                                    \
  for (size_t ht_i = 0; ht_i < (h)->capacity; ht_i++)                          \
    for (struct ht_bucket * ht_cont, *ht_b = &(h)->buckets[ht_i];              \
         (ht_cont = ht_b) && ht_b->val; ht_b = ht_b->next)                     \
      for (item = ht_b->val; ht_cont; ht_cont = NULL)

// Iterate over key-values of the hash table in unspecified order.
#define ht_foreach_kv(k, v, h)                                                 \
  for (size_t ht_i = 0; ht_i < (h)->capacity; ht_i++)                          \
    for (struct ht_bucket * ht_cont, *ht_b = &(h)->buckets[ht_i];              \
         (ht_cont = ht_b) && ht_b->val; ht_b = ht_b->next)                     \
      for (k = ht_b->key; ht_cont;)                                            \
        for (v = ht_b->val; ht_cont; ht_cont = NULL)

// Iterate over values of the linked hash table in inssertion order.
#define lht_foreach(item, t)                                                   \
  for (struct lht_bucket * lht_cont, *lht_b = (t)->first;                      \
       (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next)            \
    for (item = lht_b->val; lht_cont; lht_cont = NULL)

// Iterate over key-values of the linked hash table in inssertion order.
#define lht_foreach_kv(k, v, t)                                                \
  for (struct lht_bucket * lht_cont, *lht_b = (t)->first;                      \
       (lht_cont = lht_b) && lht_b->val; lht_b = lht_b->order_next)            \
    for (k = lht_b->key; lht_cont;)                                            \
      for (v = lht_b->val; lht_cont; lht_cont = NULL)
