#ifndef PREVIEW_H
#define PREVIEW_H

#include <stdbool.h>
#include <time.h>

#include "cvector.h"
#include "dir.h"

typedef struct preview_t {
	char *path;
	cvector_vector_type(char*) lines;
	int nrow;
	time_t mtime;
	bool loading;
} preview_t;

preview_t *preview_new(const char *path, int nrow);

preview_t *preview_new_loading(const char *path, int nrow);

preview_t *preview_new_from_file(const char *path, int nrow);

void preview_free(preview_t *pv);

#endif /* PREVIEW_H */
