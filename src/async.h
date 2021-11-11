#ifndef ASYNC_H
#define ASYNC_H

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>

#include "dir.h"
#include "preview.h"
#include "tpool.h"

enum result_e { RES_DIR_UPDATE, RES_PREVIEW };

typedef struct res_t {
	enum result_e type;
	union payload{
		struct dir_update_t {
			dir_t *dir;
			dir_t *update;
		} dir_update;
		preview_t *preview;
	} payload;
	struct res_t *next;
} res_t;

typedef struct resq_t {
	struct res_t *head;
	struct res_t *tail;
	pthread_mutex_t mutex;
	ev_async *watcher;
} resq_t;

extern tpool_t *async_tm;
extern resq_t async_results;

bool queue_get(resq_t *dirq, res_t *result);

void queue_deinit(resq_t *dirq);

void async_dir_load_delayed(dir_t *dir, int delay /* millis */);

#define async_dir_load(dir) async_dir_load_delayed(dir, -1)

void async_preview_load(const char *path, const file_t *fptr, int x, int y);

#endif /* ASYNC_H */
