#pragma once

#include "cvector.h"

/* TODO: integrate this with cmdline instead of Ui (on 2022-01-25) */

typedef struct History {
	cvector_vector_type(struct history_entry) vec;
	struct history_entry *ptr;
} History;

void history_load(History *h, const char *path);
void history_write(History *h, const char *path);
void history_append(History *h, const char *line);
void history_reset(History *h);
void history_deinit(History *h);
const char *history_next(History *h);
const char *history_prev(History *h);
