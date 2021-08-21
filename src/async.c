#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "async.h"
#include "log.h"
#include "dir.h"
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

void queue_put(resq_t *queue, enum result_e type, void *payload)
{
	res_t *r = malloc(sizeof(res_t));
	r->payload = payload;
	r->next = NULL;
	r->type = type;

	if (!queue->head) {
		queue->head = r;
		queue->tail = r;
	} else {
		queue->tail->next = r;
		queue->tail = r;
	}
}

bool queue_get(resq_t *queue, enum result_e *type, void **r)
{
	res_t *res;

	if (!(res = queue->head)) {
		return false;
	}

	*r = res->payload;
	*type = res->type;

	queue->head = res->next;
	if (queue->tail == res) {
		queue->tail = NULL;
	}
	free(res);
	return true;
}

void queue_destroy(resq_t *queue)
{
	void *r;
	enum result_e type;
	while (queue_get(queue, &type, &r)) {
		switch (type) {
		case RES_DIR:
			dir_free(r);
			break;
		case RES_PREVIEW:
			preview_free(r);
			break;
		default:
			break;
		}
	}
}

struct dir_work {
	char *path;
	int delay;
};

static void async_dir_load_worker(void *arg)
{
	struct dir_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}
	dir_t *d = dir_load(w->path);

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, RES_DIR, d);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
	free(w->path);
	free(w);
}

void async_dir_load_delayed(const char *path, int delay /* millis */)
{
	struct dir_work *w = malloc(sizeof(struct dir_work));
	w->path = strdup(path);
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
	preview_t *pv = preview_new_from_file(w->path, w->fptr, w->x, w->y);
	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, RES_PREVIEW, pv);
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
