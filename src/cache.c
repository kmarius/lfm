#include <stdlib.h>

#include "dir.h"
#include "cache.h"
#include "log.h"
#include "util.h"
#include "time.h"

#ifndef PARENT
#define PARENT(i) ((i)-1) / 2
#endif

#ifndef LCHILD
#define LCHILD(i) (2 * (i) + 1)
#endif

#ifndef RCHILD
#define RCHILD(i) (2 * (i) + 2)
#endif

typedef cache_t T;

struct node_t {
	void *data;
	int sort_key;
	const char *search_key;
};

static inline void upheap(T *t, int i);
static void downheap(T *t, int i);

static inline void swap(struct node_t *x, struct node_t *y)
{
	struct node_t tmp = *x;
	*x = *y;
	*y = tmp;
}

T *cache_new(int capacity, void (*free)(void*))
{
	T *t = malloc(sizeof(T));
	t->nodes = malloc(sizeof(struct node_t) * capacity);
	t->capacity = capacity;
	t->size = 0;
	t->free = free;
	return t;
}

void cache_resize(T *t, int capacity)
{
	if (capacity < 0) {
		return;
	}
	while (t->size > capacity) {
		t->free(t->nodes[0].data);
		t->nodes[0] = t->nodes[--t->size];
		if (t->size > 0) {
			downheap(t, 0);
		}
	}
	t->nodes = realloc(t->nodes, sizeof(struct node_t) * capacity);
	t->capacity = capacity;
}

void cache_insert(T *t, void *e, const char *key)
{
	if (t->capacity == 0) {
		return;
	}
	if (t->size >= t->capacity) {
		t->free(t->nodes[0].data);
		t->nodes[0].data = e;
		t->nodes[0].sort_key = time(NULL);
		t->nodes[0].search_key = key;
		downheap(t, 0);
	} else {
		t->nodes[t->size].data = e;
		t->nodes[t->size].sort_key = time(NULL);
		t->nodes[t->size].search_key = key;
		t->size++;
		upheap(t, t->size - 1);
	}
}

void *cache_take(T *t, const void *key)
{
	int i;
	for (i = 0; i < t->size; i++) {
		if (streq(t->nodes[i].search_key, key)) {
			void *e = t->nodes[i].data;
			if (i < t->size - 1) {
				swap(t->nodes + i, t->nodes + t->size-1);
				t->size--;

				if (i == 0 || t->nodes[i].sort_key >= t->nodes[PARENT(i)].sort_key) {
					downheap(t, i);
				} else {
					upheap(t, i);
				}
			} else {
				t->size--;
			}
			return e;
		}
	}
	return NULL;
}

static inline void upheap(T *t, int i)
{
	int p;
	while (i > 0 && t->nodes[p = PARENT(i)].sort_key > t->nodes[i].sort_key) {
		swap(&t->nodes[p], &t->nodes[i]);
		i = p;
	}
}

static void downheap(T *t, int i)
{
	const int lidx = LCHILD(i);
	const int ridx = RCHILD(i);

	int largest = i;

	if (lidx < t->size && t->nodes[lidx].sort_key < t->nodes[largest].sort_key) {
		largest = lidx;
	}

	if (ridx < t->size && t->nodes[ridx].sort_key < t->nodes[largest].sort_key) {
		largest = ridx;
	}

	if (largest != i) {
		swap(t->nodes+i, t->nodes+largest);
		downheap(t, largest);
	}
}

void cache_clear(T *t)
{
	int i;
	for (i = 0; i < t->size; i++) {
		t->free(t->nodes[i].data);
	}
	t->size = 0;
}

void cache_destroy(T *t) {
	if (t) {
		cache_clear(t);
		free(t->nodes);
		free(t);
	}
}
