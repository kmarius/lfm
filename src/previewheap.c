#include "dir.h"
#include "previewheap.h"
#include "log.h"
#include "util.h"

#ifndef parent
#define PARENT(i) ((i)-1) / 2
#endif

#ifndef left_child
#define LCHILD(i) (2 * (i) + 1)
#endif

#ifndef right_child
#define RCHILD(i) (2 * (i) + 2)
#endif

static inline void upheap(previewheap_t *heap, int i);
static void downheap(previewheap_t *heap, int i);

static inline void swap(preview_t **x, preview_t **y)
{
	preview_t *tmp = *x;
	*x = *y;
	*y = tmp;
}

void previewheap_insert(previewheap_t *heap, preview_t *d)
{
	d->access = time(NULL);
	if (heap->size >= PREVIEWHEAP_MAX_SIZE) {
		preview_free(heap->previews[0]);
		heap->previews[0] = d;
		downheap(heap, 0);
	} else {
		heap->previews[heap->size++] = d;
		upheap(heap, heap->size - 1);
	}
}

preview_t **previewheap_find(previewheap_t *heap, const char *path)
{
	int i;
	for (i = heap->size; i > 0; i--) {
		if (streq(heap->previews[i-1]->path, path)) {
			return &heap->previews[i-1];
		}
	}
	return NULL;
}

static preview_t *previewheap_takei(previewheap_t *heap, int i)
{
	if (i < 0 || i >= heap->size) {
		return NULL;
	}
	preview_t *d = heap->previews[i];
	if (i < heap->size - 1) {
		swap(heap->previews + i, heap->previews + heap->size-1);
		heap->size--;

		if (i == 0 || heap->previews[i]->access >= heap->previews[PARENT(i)]->access) {
			downheap(heap, i);
		} else {
			upheap(heap, i);
		}
	} else {
		heap->size--;
	}
	return d;
}

preview_t *previewheap_take(previewheap_t *heap, const char *path) {
	int i;
	for (i = heap->size; i > 0; i--) {
		if (streq(heap->previews[i-1]->path, path)) {
			return previewheap_takei(heap, i-1);
		}
	}
	return NULL;
}

preview_t *previewheap_ptake(previewheap_t *heap, preview_t **p)
{
	return previewheap_takei(heap, p - heap->previews);
}

static inline void upheap(previewheap_t *heap, int i)
{
	int p;
	while (i > 0 && heap->previews[p = PARENT(i)]->access > heap->previews[i]->access) {
		swap(&heap->previews[p], &heap->previews[i]);
		i = p;
	}
}

static void downheap(previewheap_t *heap, int i)
{
	const int lidx = LCHILD(i);
	const int ridx = RCHILD(i);

	int largest = i;

	if (lidx < heap->size && heap->previews[lidx]->access < heap->previews[largest]->access) {
		largest = lidx;
	}

	if (ridx < heap->size && heap->previews[ridx]->access < heap->previews[largest]->access) {
		largest = ridx;
	}

	if (largest != i) {
		swap(&heap->previews[i], &heap->previews[largest]);
		downheap(heap, largest);
	}
}

void log_previewheap(previewheap_t *heap) {
	int i;
	for (i = 0; i< heap->size; i++) {
		log_debug("%s", heap->previews[i]->path);
	}
}

void previewheap_update(previewheap_t *heap, int i, time_t access) {
	heap->previews[i]->access = access;
	if (heap->previews[i]->access < access) {
		heap->previews[i]->access = access;
		downheap(heap, i);
	} else {
		heap->previews[i]->access = access;
		upheap(heap, i);
	}
}

void previewheap_updatep(previewheap_t *heap, preview_t **p, time_t t)
{
	previewheap_update(heap, p - heap->previews, t);
}
