#ifndef CACHE_H
#define CACHE_H

typedef struct cache_t cache_t;

struct cache_t {
	struct node_t *nodes;
	int size;
	int capacity;
	void (*free)(void*);
};

cache_t *cache_new(int capacity, void (*free)(void*));
void cache_resize(cache_t *heap, int capacity);
void cache_insert(cache_t *heap, void *e, const char *key);
void *cache_take(cache_t *heap, const void *key);
void cache_clear(cache_t *heap);
void cache_destroy(cache_t *heap);

#endif /* CACHE_H */
