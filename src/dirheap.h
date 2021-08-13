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
 * Update the directory pointed at by p with the access time t.
 */
void dirheap_pupdate(dirheap_t *heap, dir_t **p, time_t t);

/*
 * Find the directory with path PATH, returns NULL if none found.
 */
dir_t **dirheap_find(dirheap_t *heap, const char *path);

/*
 * Take the directory with path PATH out of the heap, returns NULL if not found.
 */
dir_t *dirheap_take(dirheap_t *heap, const char *path);

/*
 * Take out directory pointed at by p (i.e. after callin dirheap_find);
 */
dir_t *dirheap_ptake(dirheap_t *heap, dir_t **p);

#endif
