#include <stdint.h>
#include <stdlib.h>

#include "cache.h"
#include "time.h"
#include "util.h"
#include "log.h"

#define T Cache

#define PARENT(i) ((i)-1) / 2
#define LCHILD(i) (2 * (i) + 1)
#define RCHILD(i) (2 * (i) + 2)

struct node {
	void *ptr;
	uint16_t sort_key;
	const char *search_key;
	bool in_use;
};


void cache_return(T *t, void *e, const char *key);


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
	do {
		const uint16_t l_idx = LCHILD(i);
		const uint16_t r_idx = RCHILD(i);
		uint16_t smallest = i;
		if (l_idx < size && a[l_idx].sort_key < a[smallest].sort_key)
			smallest = l_idx;
		if (r_idx < size && a[r_idx].sort_key < a[smallest].sort_key)
			smallest = r_idx;
		if (smallest == i)
			break;
		swap(a+i, a+smallest);
		i = smallest;
	} while (1);
}


void cache_init(T *t, uint16_t capacity, void (*free)(void*))
{
	t->nodes = malloc(sizeof(struct node) * capacity);
	t->capacity = capacity;
	t->size = 0;
	t->free = free;
	t->version = 0;
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


void cache_insert(T *t, void *e, const char *key, bool in_use)
{
	/* TODO: free here? (on 2022-02-06) */
	if (t->capacity == 0)
		return;

	for (uint16_t i = 0; i < t->size; i++) {
		if (t->nodes[i].ptr == e){
			t->nodes[i].sort_key = time(NULL);
			t->nodes[i].in_use = in_use;
			return;
		}
	}

	const uint16_t sort_key = in_use ? UINT16_MAX : time(NULL);
	if (t->size >= t->capacity) {
		if (t->nodes[0].in_use) {
			log_error("can not free used dir %s", key);
			return;
		}
		t->free(t->nodes[0].ptr);
		t->nodes[0].ptr = e;
		t->nodes[0].sort_key = sort_key;
		t->nodes[0].search_key = key;
		t->nodes[0].in_use = in_use;
		downheap(t->nodes, t->size, 0);
	} else {
		t->nodes[t->size].ptr = e;
		t->nodes[t->size].sort_key = sort_key;
		t->nodes[t->size].search_key = key;
		t->nodes[t->size].in_use = in_use;
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
			void *ptr = t->nodes[i].ptr;
			t->nodes[i].sort_key = UINT16_MAX;
			t->nodes[i].in_use = true;
			downheap(t->nodes, t->size, i);
			return ptr;
		}
	}
	return NULL;
}


void cache_drop(T *t)
{
	uint16_t j = 0;
	for (uint16_t i = 0; i < t->size; i++) {
		if (t->nodes[i].in_use)
			t->nodes[j++] = t->nodes[i];
		else
			t->free(t->nodes[i].ptr);
	}
	t->size = j;
	t->version++;
}


void cache_deinit(T *t) {
	cache_drop(t);
	free(t->nodes);
}

#undef T
