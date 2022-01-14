#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	struct node *nodes;
	uint16_t size;
	uint16_t capacity;
	void (*free)(void*);
} Cache;

void cache_init(Cache *cache, uint16_t capacity, void (*free)(void*));
void cache_resize(Cache *cache, uint16_t capacity);
void cache_insert(Cache *cache, void *e, const char *key);
bool cache_contains_ptr(Cache *cache, const void *ptr);
void *cache_find(Cache *cache, const void *key);
void *cache_take(Cache *cache, const void *key);
void cache_clear(Cache *cache);
void cache_deinit(Cache *cache);
