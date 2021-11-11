#ifndef CACHE_H
#define CACHE_H

typedef struct cache_t cache_t;

struct cache_t {
	struct node_t *nodes;
	int size;
	int capacity;
	void (*free)(void*);
};

void cache_init(cache_t *cache, int capacity, void (*free)(void*));
void cache_resize(cache_t *cache, int capacity);
void cache_insert(cache_t *cache, void *e, const char *key);
void *cache_find(cache_t *cache, const void *key);
void *cache_take(cache_t *cache, const void *key);
void cache_clear(cache_t *cache);
void cache_deinit(cache_t *cache);

#endif /* CACHE_H */
