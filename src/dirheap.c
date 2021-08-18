#include "dir.h"
#include "dirheap.h"
#include "log.h"
#include "util.h"

#ifndef PARENT
#define PARENT(i) ((i)-1) / 2
#endif

#ifndef LCHILD
#define LCHILD(i) (2 * (i) + 1)
#endif

#ifndef RCHILD
#define RCHILD(i) (2 * (i) + 2)
#endif

static inline void upheap(dirheap_t *heap, int i);
static void downheap(dirheap_t *heap, int i);
/* static void log_dirheap(dirheap_t *heap); */

static inline void swap(dir_t **x, dir_t **y)
{
	dir_t *tmp = *x;
	*x = *y;
	*y = tmp;
}

void dirheap_insert(dirheap_t *heap, dir_t *d)
{
	/* log_trace("dirheap_insert sz %d %s", heap->size, d->name); */
	d->access = time(NULL);
	if (heap->size >= DIRHEAP_MAX_SIZE) {
		log_trace("free_dir %s %p", heap->dirs[0]->path, heap->dirs[0]);
		dir_free(heap->dirs[0]);
		heap->dirs[0] = d;
		downheap(heap, 0);
	} else {
		/* insert as back, upheap shouldn't be necessary because keys are
		 * monotonuously increasing */
		heap->dirs[heap->size++] = d;
		upheap(heap, heap->size - 1);
	}
	/* log_debug("---after inserting %s ---", d->name); */
	/* log_dirheap(heap); */
}

dir_t **dirheap_find(dirheap_t *heap, const char *path)
{
	int i;
	for (i = heap->size; i > 0; i--) {
		if (streq(heap->dirs[i-1]->path, path)) {
			return &heap->dirs[i-1];
		}
	}
	return NULL;
}

/* Take out an element of the heap by swapping it with the last element and
 * restoring the heap property. */
static dir_t *dirheap_takei(dirheap_t *heap, int i)
{
	/* log_trace("dirheap_take i %d, size %d", i, heap->size); */
	if (i < 0 || i >= heap->size) {
		return NULL;
	}
	dir_t *d = heap->dirs[i];
	if (i < heap->size - 1) {
		swap(heap->dirs + i, heap->dirs + heap->size-1);
		heap->size--;

		if (i == 0 || heap->dirs[i]->access >= heap->dirs[PARENT(i)]->access) {
			downheap(heap, i);
		} else {
			upheap(heap, i);
		}
	} else {
		heap->size--;
	}
	return d;
}

dir_t *dirheap_take(dirheap_t *heap, const char *path) {
	int i;
	for (i = heap->size; i > 0; i--) {
		if (streq(heap->dirs[i-1]->path, path)) {
			return dirheap_takei(heap, i-1);
		}
	}
	return NULL;
}

dir_t *dirheap_ptake(dirheap_t *heap, dir_t **p)
{
	return dirheap_takei(heap, p - heap->dirs);
}

static inline void upheap(dirheap_t *heap, int i)
{
	int p;
	while (i > 0 && heap->dirs[p = PARENT(i)]->access > heap->dirs[i]->access) {
		swap(&heap->dirs[p], &heap->dirs[i]);
		i = p;
	}
}

static void downheap(dirheap_t *heap, int i)
{
	/* log_debug("downheap %s", dirs[i]->name); */

	const int lidx = LCHILD(i);
	const int ridx = RCHILD(i);

	int largest = i;

	if (lidx < heap->size && heap->dirs[lidx]->access < heap->dirs[largest]->access) {
		largest = lidx;
	}

	if (ridx < heap->size && heap->dirs[ridx]->access < heap->dirs[largest]->access) {
		largest = ridx;
	}

	if (largest != i) {
		swap(&heap->dirs[i], &heap->dirs[largest]);
		downheap(heap, largest);
	}
}

void log_dirheap(dirheap_t *heap) {
	int i;
	for (i = 0; i< heap->size; i++) {
		log_debug("%s", heap->dirs[i]->name);
	}
}
