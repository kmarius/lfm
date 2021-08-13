#ifndef ASYNC_H
#define ASYNC_H

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>

#include "dir.h"

enum result_e { RES_DIR, RES_PREVIEW };

typedef struct Result {
	enum result_e type;
	void *payload;
	struct Result *next;
} res_t;

typedef struct ResultQueue {
	struct Result *head;
	struct Result *tail;
	pthread_mutex_t mutex;
	ev_async *watcher;
} resq_t;

bool queue_get(resq_t *dirq, res_t **result);

void queue_clear(resq_t *dirq);

void async_load_dir_delayed(const char *path, int delay /* millis */);

#define async_load_dir(path) async_load_dir_delayed(path, -1)

void async_load_preview(const char *path, const file_t *fptr, int x, int y);

#endif
