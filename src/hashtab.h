#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal hash table to be used in directory/preview caches.

typedef void (*free_fun)(void *);

struct ht_bucket {
  const char *key;
  void *val;
  struct ht_bucket *next;
};

typedef struct hashtab {
  struct ht_bucket *buckets;
  uint16_t nbuckets;
  free_fun free;
  uint8_t version;
} Hashtab;

Hashtab *ht_init(Hashtab *t, size_t size, free_fun free);
void ht_deinit(Hashtab *t);
void ht_set(Hashtab *t, const char *key, void *val);
void *ht_get(Hashtab *t, const char *key);
void ht_clear(Hashtab *t);

struct ht_stats {
  uint16_t nbuckets;
  uint16_t nelems;
  uint16_t bucket_size_max;
  uint16_t buckets_nonempty;
  double bucket_nonempty_avg_size;
  double alpha;
};

struct ht_stats ht_stats(Hashtab *t);

// iterate over values
#define ht_foreach(item, h) \
  for (size_t ht_i = 0; ht_i < (h)->nbuckets; ht_i++) \
  for (struct ht_bucket *ht_cont, *ht_b = &(h)->buckets[ht_i]; \
      (ht_cont = ht_b) && ht_b->val; ht_b = ht_b->next) \
  for (item = ht_b->val; ht_cont; ht_cont = NULL)
