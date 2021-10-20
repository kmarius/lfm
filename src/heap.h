#ifndef HEAP_H
#define HEAP_H

typedef struct heap_t heap_t;

struct heap_t {
	struct heap_node_t *nodes;
	int size;
	int capacity;
	void (*free)(void*);
};

heap_t *heap_new(int capacity, void (*free_fun)(void*));
void heap_resize(heap_t *heap, int capacity);
void heap_insert(heap_t *heap, void *e, const char *key);
void *heap_take(heap_t *heap, const void *key);
void heap_empty(heap_t *heap);
void heap_destroy(heap_t *heap);

#endif
