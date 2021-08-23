#include <stdlib.h>

#include "heap.h"
#include "log.h"
#include "util.h"
#include "time.h"

#ifndef parent
#define PARENT(i) ((i)-1) / 2
#endif

#ifndef leftchild
#define LCHILD(i) (2 * (i) + 1)
#endif

#ifndef rightchild
#define RCHILD(i) (2 * (i) + 2)
#endif

static inline void upheap(heap_t *heap, int i);
static void downheap(heap_t *heap, int i);

static inline void swap(struct heap_node_t *x, struct heap_node_t *y)
{
	struct heap_node_t tmp = *x;
	*x = *y;
	*y = tmp;
}

heap_t *heap_new(int capacity, void (*free_fun)(void*))
{
	heap_t *heap = malloc(sizeof(heap_t));
	heap->nodes = malloc(sizeof(struct heap_node_t) * capacity);
	heap->capacity = capacity;
	heap->size = 0;
	heap->free_fun = free_fun;
	return heap;
}

void heap_insert(heap_t *heap, void *e)
{
	if (heap->size >= heap->capacity) {
		heap->free_fun(heap->nodes[0].data);
		heap->nodes[0].data = e;
		heap->nodes[0].key = time(NULL);
		downheap(heap, 0);
	} else {
		heap->nodes[heap->size].data = e;
		heap->nodes[heap->size++].key = time(NULL);
		upheap(heap, heap->size - 1);
	}
}

void *heap_take(heap_t *heap, int (*eq_fun)(void*, const void*), const void *a)
{
	int i;
	for (i = 0; i < heap->size; i++) {
		if (eq_fun(heap->nodes[i].data, a)) {
			void *e = heap->nodes[i].data;
			if (i < heap->size - 1) {
				swap(heap->nodes + i, heap->nodes + heap->size-1);
				heap->size--;

				if (i == 0 || heap->nodes[i].key >= heap->nodes[PARENT(i)].key) {
					downheap(heap, i);
				} else {
					upheap(heap, i);
				}
			} else {
				heap->size--;
			}
			return e;
		}
	}
	return NULL;
}

static inline void upheap(heap_t *heap, int i)
{
	int p;
	while (i > 0 && heap->nodes[p = PARENT(i)].key > heap->nodes[i].key) {
		swap(&heap->nodes[p], &heap->nodes[i]);
		i = p;
	}
}

static void downheap(heap_t *heap, int i)
{
	const int lidx = LCHILD(i);
	const int ridx = RCHILD(i);

	int largest = i;

	if (lidx < heap->size && heap->nodes[lidx].key < heap->nodes[largest].key) {
		largest = lidx;
	}

	if (ridx < heap->size && heap->nodes[ridx].key < heap->nodes[largest].key) {
		largest = ridx;
	}

	if (largest != i) {
		swap(heap->nodes+i, heap->nodes+largest);
		downheap(heap, largest);
	}
}

void heap_empty(heap_t *heap)
{
	int i;
	for (i = 0; i < heap->size; i++) {
		heap->free_fun(heap->nodes[i].data);
	}
	heap->size = 0;
}

void heap_destroy(heap_t *heap) {
	if (heap) {
		heap_empty(heap);
		free(heap->nodes);
		free(heap);
	}
}
