#include <stdlib.h>

#include "cache.h"
#include "util.h"
#include "time.h"

#define T cache_t
#define PARENT(i) ((i)-1) / 2
#define LCHILD(i) (2 * (i) + 1)
#define RCHILD(i) (2 * (i) + 2)

struct node_t {
	void *data;
	int sort_key;
	const char *search_key;
};

static inline void swap(struct node_t *x, struct node_t *y)
{
	struct node_t tmp = *x;
	*x = *y;
	*y = tmp;
}

static inline void upheap(struct node_t *a, int i)
{
	int p;
	while (i > 0 && a[p = PARENT(i)].sort_key > a[i].sort_key) {
		swap(a+p, a+i);
		i = p;
	}
}

static void downheap(struct node_t *a, int size, int i)
{
	const int lidx = LCHILD(i);
	const int ridx = RCHILD(i);

	int largest = i;

	if (lidx < size && a[lidx].sort_key < a[largest].sort_key) {
		largest = lidx;
	}

	if (ridx < size && a[ridx].sort_key < a[largest].sort_key) {
		largest = ridx;
	}

	if (largest != i) {
		swap(a+i, a+largest);
		downheap(a, size, largest);
	}
}

void cache_init(T *t, int capacity, void (*free)(void*))
{
	t->nodes = malloc(sizeof(struct node_t) * capacity);
	t->capacity = capacity;
	t->size = 0;
	t->free = free;
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
			downheap(t->nodes, t->size, 0);
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
		downheap(t->nodes, t->size, 0);
	} else {
		t->nodes[t->size].data = e;
		t->nodes[t->size].sort_key = time(NULL);
		t->nodes[t->size].search_key = key;
		t->size++;
		upheap(t->nodes, t->size - 1);
	}
}

void *cache_find(T *t, const void *key)
{
	int i;
	for (i = 0; i < t->size; i++) {
		if (streq(t->nodes[i].search_key, key)) {
			return t->nodes[i].data;
		}
	}
	return NULL;
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
					downheap(t->nodes, t->size, i);
				} else {
					upheap(t->nodes, i);
				}
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
	int i;
	for (i = 0; i < t->size; i++) {
		t->free(t->nodes[i].data);
	}
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
