#ifndef PREVIEWHEAP_H
#define PREVIEWHEAP_H

#include "preview.h"

#define PREVIEWHEAP_MAX_SIZE 31

/*
 * Directories are stored as a min heap so that the least recent accessed dir
 * stays at the top to be freed for a new dir. Dirs that are currently shown in
 * the ui are excluded from this.
 */

typedef struct previewheap {
	preview_t *previews[PREVIEWHEAP_MAX_SIZE];
	int size;
} previewheap_t;

/*
 * Insert a directory into the heap. If the heap is full, the directory at the
 * root node (i.e. the one longest not accessed) will be freed to make space.
 */
void previewheap_insert(previewheap_t *heap, preview_t *pv);

/*
 * Update the directory pointed at by p with the access time t.
 */
void previewheap_updatep(previewheap_t *heap, preview_t **p, time_t t);

/*
 * Find the directory with path PATH, returns NULL if none found.
 */
preview_t **previewheap_find(previewheap_t *heap, const char *path);

/*
 * Take the directory with path PATH out of the heap, returns NULL if not found.
 */
preview_t *previewheap_take(previewheap_t *heap, const char *path);

/*
 * Take out directory pointed at by p (i.e. after callin previewheap_find);
 */
preview_t *previewheap_ptake(previewheap_t *heap, preview_t **p);

#endif
