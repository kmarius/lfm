#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Cache {
	struct node *nodes;
	uint16_t size;
	uint16_t capacity;
	uint8_t version;
	void (*free)(void*);
} Cache;

void cache_init(Cache *cache, uint16_t capacity, void (*free)(void*));
void cache_deinit(Cache *cache);
void cache_resize(Cache *cache, uint16_t capacity);
void cache_insert(Cache *cache, void *e, const char *key, bool in_use);
static inline void cache_return(Cache *cache, void *e, const char *key) {
	cache_insert(cache, e, key, false);
}
bool cache_contains_ptr(Cache *cache, const void *ptr);
void *cache_find(Cache *cache, const void *key);
void *cache_take(Cache *cache, const void *key);
void cache_drop(Cache *cache);
