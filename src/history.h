#ifndef HISTORY_H
#define HISTORY_H

#include "cvector.h"

typedef struct history_t {
	cvector_vector_type(struct node) vec;
	struct node *ptr;
} history_t;

void history_load(history_t *h, const char *path);
void history_write(history_t *h, const char *path);
void history_append(history_t *h, const char *line);
void history_reset(history_t *h);
void history_clear(history_t *h);
const char *history_next(history_t *h);
const char *history_prev(history_t *h);

#endif /* HISTORY_H */
