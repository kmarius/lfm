#include <stdlib.h>

#include "cache.h"
#include "time.h"
#include "util.h"

#define T Cache
#define PARENT(i) ((i)-1) / 2
#define LCHILD(i) (2 * (i) + 1)
#define RCHILD(i) (2 * (i) + 2)

struct node {
	void *ptr;
	uint16_t sort_key;
	const char *search_key;
};

static inline void swap(struct node *x, struct node *y)
{
	struct node tmp = *x;
	*x = *y;
	*y = tmp;
}

static inline void upheap(struct node *nodes, int16_t i)
{
	int16_t p;
	while (i > 0 && nodes[p = PARENT(i)].sort_key > nodes[i].sort_key) {
		swap(nodes+p, nodes+i);
		i = p;
	}
}

static void downheap(struct node *a, uint16_t size, uint16_t i)
{
	const uint16_t lidx = LCHILD(i);
	const uint16_t ridx = RCHILD(i);

	uint16_t largest = i;

	if (lidx < size && a[lidx].sort_key < a[largest].sort_key)
		largest = lidx;

	if (ridx < size && a[ridx].sort_key < a[largest].sort_key)
		largest = ridx;


	if (largest != i) {
		swap(a+i, a+largest);
		downheap(a, size, largest);
	}
}

void cache_init(T *t, uint16_t capacity, void (*free)(void*))
{
	t->nodes = malloc(sizeof(struct node) * capacity);
	t->capacity = capacity;
	t->size = 0;
	t->free = free;
}

void cache_resize(T *t, uint16_t capacity)
{
	while (t->size > capacity) {
		t->free(t->nodes[0].ptr);
		t->nodes[0] = t->nodes[--t->size];
		if (t->size > 0)
			downheap(t->nodes, t->size, 0);

	}
	t->nodes = realloc(t->nodes, sizeof(struct node) * capacity);
	t->capacity = capacity;
}

void cache_insert(T *t, void *e, const char *key)
{
	if (t->capacity == 0)
		return;

	if (t->size >= t->capacity) {
		t->free(t->nodes[0].ptr);
		t->nodes[0].ptr = e;
		t->nodes[0].sort_key = time(NULL);
		t->nodes[0].search_key = key;
		downheap(t->nodes, t->size, 0);
	} else {
		t->nodes[t->size].ptr = e;
		t->nodes[t->size].sort_key = time(NULL);
		t->nodes[t->size].search_key = key;
		t->size++;
		upheap(t->nodes, t->size - 1);
	}
}

bool cache_contains_ptr(T *t, const void *ptr)
{
	for (uint16_t i = 0; i < t->size; i++)
		if (t->nodes[i].ptr == ptr)
			return true;
	return false;
}

void *cache_find(T *t, const void *key)
{
	for (uint16_t i = 0; i < t->size; i++)
		if (streq(t->nodes[i].search_key, key))
			return t->nodes[i].ptr;
	return NULL;
}

void *cache_take(T *t, const void *key)
{
	for (uint16_t i = 0; i < t->size; i++) {
		if (streq(t->nodes[i].search_key, key)) {
			void *e = t->nodes[i].ptr;
			if (i < t->size - 1) {
				swap(t->nodes + i, t->nodes + t->size-1);
				t->size--;

				if (i == 0 || t->nodes[i].sort_key >= t->nodes[PARENT(i)].sort_key)
					downheap(t->nodes, t->size, i);
				else
					upheap(t->nodes, i);

			} else {
				t->size--;
			}
			return e;
		}
	}
	return NULL;
}

void cache_clear(T *t)
{
	for (uint16_t i = 0; i < t->size; i++)
		t->free(t->nodes[i].ptr);
	t->size = 0;
}

void cache_deinit(T *t) {
	cache_clear(t);
	free(t->nodes);
}

#undef T
#undef RCHILD
#undef LCHILD
#undef PARENT
