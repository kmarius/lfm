#ifndef PREVIEW_H
#define PREVIEW_H

#include <time.h>

#include "cvector.h"
#include "dir.h"

typedef struct Preview {
	time_t access; /* key for the cache/heap, keep as first field */
	const file_t *fptr;
	char *path;
	cvector_vector_type(char*) lines;
	time_t mtime;
} preview_t;

preview_t *preview_new(const char *path, const file_t *fptr, int nrow, int ncol);

preview_t *preview_new_loading(const char *path, const file_t *fptr, int nrow,
			       int ncol);

preview_t *preview_new_from_file(const char *path, const file_t *fptr, int nrow, int ncol);

bool preview_check(const preview_t *pv);

void preview_free(preview_t *pv);

#endif
