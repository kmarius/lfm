#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "async.h"
#include "dir.h"
#include "log.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

tpool_t *async_tm;

resq_t async_results = {
	.head = NULL,
	.tail = NULL,
	.watcher = NULL,
};

void queue_put(resq_t *queue, res_t res)
{
	res_t *r = malloc(sizeof(res_t));
	*r = res;

	if (!queue->head) {
		queue->head = r;
		queue->tail = r;
	} else {
		queue->tail->next = r;
		queue->tail = r;
	}
}

bool queue_get(resq_t *queue, res_t *result)
{
	res_t *res;

	if (!(res = queue->head)) {
		return false;
	}

	*result = *res;
	result->next = NULL;

	queue->head = res->next;
	if (queue->tail == res) {
		queue->tail = NULL;
	}
	free(res);

	return true;
}

void queue_deinit(resq_t *queue)
{
	res_t result;
	while (queue_get(queue, &result)) {
		switch (result.type) {
			case RES_DIR_UPDATE:
				dir_free(result.payload.dir_update.update);
				break;
			case RES_PREVIEW:
				preview_free(result.payload.preview);
				break;
			default:
				break;
		}
	}
}

struct dir_work {
	dir_t *dir;
	int delay;
};

static void async_dir_load_worker(void *arg)
{
	struct dir_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}
	res_t r = {
		.type = RES_DIR_UPDATE,
		.payload = {{
			.dir=w->dir,
			.update=dir_load(w->dir->path, 1)
		}},
	};

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_dir_load_delayed(dir_t *dir, int delay /* millis */)
{
	struct dir_work *w = malloc(sizeof(struct dir_work));
	w->dir = dir;
	w->delay = delay;
	tpool_add_work(async_tm, async_dir_load_worker, w);
}

struct pv_work {
	char *path;
	const file_t *fptr;
	int x;
	int y;
};

static void async_preview_load_worker(void *arg)
{
	struct pv_work *w = (struct pv_work*) arg;
	res_t r = {
		.type = RES_PREVIEW,
		.payload = {
			.preview = preview_new_from_file(w->path, w->fptr, w->x, w->y),
		},
	};
	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);
	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
	free(w->path);
	free(w);
}

void async_preview_load(const char *path, const file_t *fptr, int x, int y)
{
	struct pv_work *w = malloc(sizeof(struct pv_work));
	w->fptr = fptr;
	w->path = strdup(path);
	w->x = x;
	w->y = y;
	tpool_add_work(async_tm, async_preview_load_worker, w);
}
