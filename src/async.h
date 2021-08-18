#ifndef ASYNC_H
#define ASYNC_H

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>

#include "dir.h"
#include "tpool.h"


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

extern tpool_t *async_tm;
extern resq_t async_results;

bool queue_get(resq_t *dirq, enum result_e *type, void **result);

void queue_clear(resq_t *dirq);

void async_dir_load_delayed(const char *path, int delay /* millis */);

#define async_dir_load(path) async_dir_load_delayed(path, -1)

void async_preview_load(const char *path, const file_t *fptr, int x, int y);

#endif
