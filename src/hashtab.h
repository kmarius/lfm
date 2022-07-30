#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal hash table to be used in directory/preview caches.

typedef void (*free_fun)(void *);

struct bucket {
  const char *key;
  void *val;
  struct bucket *next;
};

typedef struct hashmap {
  struct bucket *buckets;
  uint16_t nbuckets;
  free_fun free;
  uint8_t version;
} Hashtab;

Hashtab *hashtab_init(Hashtab *t, size_t size, free_fun free);
void hashtab_deinit(Hashtab *t);
void hashtab_set(Hashtab *t, const char *key, void *val);
void *hashtab_get(Hashtab *t, const char *key);
void hashtab_clear(Hashtab *t);

struct ht_stats {
  uint16_t nbuckets;
  uint16_t nelems;
  uint16_t bucket_size_max;
  uint16_t buckets_nonempty;
  double bucket_nonempty_avg_size;
  double alpha;
};

struct ht_stats hashtab_stats(Hashtab *t);

#define hashtab_foreach(item, h) \
  for (size_t hm_i = 0; hm_i < (h)->nbuckets; hm_i++) \
  for (struct bucket *hm_cont, *b = &(h)->buckets[hm_i]; (hm_cont = b) && b->val; b = b->next) \
  for (item = b->val; hm_cont; hm_cont = NULL)
