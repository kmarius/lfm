#ifndef DIRHEAP_H
#define DIRHEAP_H

#include "dir.h"

#define DIRHEAP_MAX_SIZE 31

/*
 * Directories are stored as a min heap so that the least recent accessed dir
 * stays at the top to be freed for a new dir. Dirs that are currently shown in
 * the ui are excluded from this.
 */

typedef struct dirheap {
	dir_t *dirs[DIRHEAP_MAX_SIZE];
	int size;
} dirheap_t;

/*
 * Insert a directory into the heap. If the heap is full, the directory at the
 * root node (i.e. the one longest not accessed) will be freed to make space.
 */
void dirheap_insert(dirheap_t *heap, dir_t *d);

/*
 * Take the directory with path PATH out of the heap, returns NULL if not found.
 */
dir_t *dirheap_take(dirheap_t *heap, const char *path);

#endif
