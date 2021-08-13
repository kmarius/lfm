#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "async.h"
#include "dir.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

resq_t results;
tpool_t *tm;

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

bool queue_get(resq_t *queue, res_t **r)
{
	if (!(*r = queue->head)) {
		return false;
	}
	queue->head = (*r)->next;
	if (queue->tail == *r) {
		queue->tail = NULL;
	}
	return true;
}

void queue_clear(resq_t *queue)
{
	res_t *r;
	while (queue_get(queue, &r)) {
		switch (r->type) {
		case RES_DIR:
			free_dir(r->payload);
			break;
		case RES_PREVIEW:
			free_preview(r->payload);
			break;
		default:
			break;
		}
		free(r);
	}
}

struct dir_work {
	char *path;
	int delay;
};

static void async_load_dir_worker(void *arg)
{
	struct dir_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}
	dir_t *d = load_dir(w->path);

	pthread_mutex_lock(&results.mutex);
	queue_put(&results, RES_DIR, d);
	pthread_mutex_unlock(&results.mutex);

	if (results.watcher) {
		ev_async_send(EV_DEFAULT_ results.watcher);
	}
	free(w->path);
	free(w);
}

void async_load_dir_delayed(const char *path, int delay /* millis */)
{
	struct dir_work *w = malloc(sizeof(struct dir_work));
	w->path = strdup(path);
	w->delay = delay;
	tpool_add_work(tm, async_load_dir_worker, w);
}

struct pv_work {
	char *path;
	const file_t *fptr;
	int x;
	int y;
};

static void async_load_pv_worker(void *arg)
{
	struct pv_work *w = (struct pv_work*) arg;
	preview_t *pv = new_file_preview(w->path, w->fptr, w->x, w->y);
	pthread_mutex_lock(&results.mutex);
	queue_put(&results, RES_PREVIEW, pv);
	pthread_mutex_unlock(&results.mutex);
	if (results.watcher) {
		ev_async_send(EV_DEFAULT_ results.watcher);
	}
	free(w->path);
	free(w);
}

void async_load_preview(const char *path, const file_t *fptr, int x, int y)
{
	struct pv_work *w = malloc(sizeof(struct pv_work));
	w->fptr = fptr;
	w->path = strdup(path);
	w->x = x;
	w->y = y;
	tpool_add_work(tm, async_load_pv_worker, w);
}
