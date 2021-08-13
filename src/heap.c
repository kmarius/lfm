#include "heap.h"
#include "log.h"
#include "util.h"
#include "time.h"

#ifndef parent
#define parent(i) ((i)-1) / 2
#endif

#ifndef leftchild
#define leftchild(i) (2 * (i) + 1)
#endif

#ifndef rightchild
#define rightchild(i) (2 * (i) + 2)
#endif

typedef struct KeyAble {
	time_t key;
} _k;

static inline void upheap(heap_t *heap, int i);
static void downheap(heap_t *heap, int i);

static inline void swap(void **x, void **y)
{
	void *tmp = *x;
	*x = *y;
	*y = tmp;
}

void heap_insert(heap_t *heap, void *e, ffun f)
{
	if (heap->size >= HEAP_MAX_SIZE) {
		f(heap->nodes[0]);
		heap->nodes[0] = e;
		downheap(heap, 0);
	} else {
		heap->nodes[heap->size++] = e;
		upheap(heap, heap->size - 1);
	}
}

static void *heap_itake(heap_t *heap, int i)
{
	if (i < 0 || i >= heap->size) {
		return NULL;
	}
	void *e = heap->nodes[i];
	if (i < heap->size - 1) {
		swap(heap->nodes + i, heap->nodes + heap->size-1);
		heap->size--;

		if (i == 0 || ((_k*)heap->nodes[i])->key >= ((_k*)heap->nodes[parent(i)])->key) {
			downheap(heap, i);
		} else {
			upheap(heap, i);
		}
	} else {
		heap->size--;
	}
	return e;
}

void *heap_ptake(heap_t *heap, void **p)
{
	return heap_itake(heap, p - heap->nodes);
}

static inline void upheap(heap_t *heap, int i)
{
	int p;
	while (i > 0 && ((_k*)heap->nodes[p = parent(i)])->key > ((_k*)heap->nodes[i])->key) {
		swap(&heap->nodes[p], &heap->nodes[i]);
		i = p;
	}
}

static void downheap(heap_t *heap, int i)
{
	const int lidx = leftchild(i);
	const int ridx = rightchild(i);

	int largest = i;

	if (lidx < heap->size && ((_k*)heap->nodes[lidx])->key < ((_k*)heap->nodes[largest])->key) {
		largest = lidx;
	}

	if (ridx < heap->size && ((_k*)heap->nodes[ridx])->key < ((_k*)heap->nodes[largest])->key) {
		largest = ridx;
	}

	if (largest != i) {
		swap(&heap->nodes[i], &heap->nodes[largest]);
		downheap(heap, largest);
	}
}

void heap_iupdate(heap_t *heap, int i, time_t t) {
	((_k*)heap->nodes[i])->key = t;
	if (((_k*)heap->nodes[i])->key < t) {
		((_k*)heap->nodes[i])->key = t;
		downheap(heap, i);
	} else {
		((_k*)heap->nodes[i])->key = t;
		upheap(heap, i);
	}
}

void heap_pupdate(heap_t *heap, void **p, time_t t)
{
	heap_iupdate(heap, p - heap->nodes, t);
}
