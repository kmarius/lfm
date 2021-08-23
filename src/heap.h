#ifndef HEAP_H
#define HEAP_H

#include <time.h>

#define HEAP_MAX_SIZE 31

typedef struct heap {
	void *nodes[HEAP_MAX_SIZE];
	int size;
} heap_t;

typedef void (*ffun)(void *);

void heap_insert(heap_t *heap, void *e, ffun f);
void heap_pupdate(heap_t *heap, void **p, time_t t);
void *heap_ptake(heap_t *heap, void **p);

#endif
