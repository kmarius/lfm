#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal hash table to be used in directory/preview caches.

struct bucket;
typedef void (*free_fun)(void *);

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
