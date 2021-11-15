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

	if (queue->head == NULL) {
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

	if ((res = queue->head) == NULL) {
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
				dir_free(result.update);
				break;
			case RES_DIR_CHECK:
				break;
			case RES_PREVIEW:
				preview_free(result.preview);
				break;
			case RES_PREVIEW_CHECK:
				free(result.path);
				break;
			default:
				break;
		}
	}
}

struct dir_check_work {
	dir_t *dir;
	time_t loadtime;
};

static void async_dir_check_worker(void *arg)
{
	struct dir_check_work *w = arg;
	struct stat statbuf;

	if (stat(w->dir->path, &statbuf) == -1) {
		free(w);
		return;
	}

	if (statbuf.st_mtime <= w->loadtime) {
		free(w);
		return;
	}

	res_t r = {
		.type = RES_DIR_CHECK,
		.dir = w->dir,
	};

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_dir_check(dir_t *dir)
{
	struct dir_check_work *w = malloc(sizeof(struct dir_check_work));
	w->dir = dir;
	w->loadtime = dir->loadtime;
	tpool_add_work(async_tm, async_dir_check_worker, w);
}

struct dir_work {
	dir_t *dir;
	int delay;
	bool dircounts;
};

static void async_dir_load_worker(void *arg)
{
	struct dir_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}
	res_t r = {
		.type = RES_DIR_UPDATE,
		.dir = w->dir,
		/* TODO: this is unsafe when dropping cache since dir might be freed (on 2021-11-15) */
		.update = dir_load(w->dir->path, w->dircounts)
	};

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	if (!w->dircounts) {
		w->dircounts = true;
		w->delay = -1;
		async_dir_load_worker(w);
		return;
	}

	free(w);
}

void async_dir_load_delayed(dir_t *dir, bool dircounts, int delay /* millis */)
{
	struct dir_work *w = malloc(sizeof(struct dir_work));
	w->dir = dir;
	w->delay = delay;
	w->dircounts = dircounts;
	tpool_add_work(async_tm, async_dir_load_worker, w);
}

struct pv_check_work {
	char *path;
	int nrow;
	time_t mtime;
};

static void async_preview_check_worker(void *arg)
{
	struct pv_check_work *w = arg;
	struct stat statbuf;

	if (stat(w->path, &statbuf) == -1) {
		free(w->path);
		free(w);
		return;
	}

	if (statbuf.st_mtime <= w->mtime) {
		free(w->path);
		free(w);
		return;
	}

	res_t r = {
		.type = RES_PREVIEW_CHECK,
		.path = w->path,
		.nrow = w->nrow,
	};

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_preview_check(preview_t *pv)
{
	struct pv_check_work *w = malloc(sizeof(struct pv_check_work));
	w->path = strdup(pv->path);
	w->nrow = pv->nrow;
	w->mtime = pv->mtime;
	tpool_add_work(async_tm, async_preview_check_worker, w);
}

struct pv_load_work {
	char *path;
	int nrow;
};

static void async_preview_load_worker(void *arg)
{
	struct pv_load_work *w = (struct pv_load_work*) arg;
	res_t r = {
		.type = RES_PREVIEW,
		.preview = preview_new_from_file(w->path, w->nrow),
	};
	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, r);
	pthread_mutex_unlock(&async_results.mutex);
	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
	free(w->path);
	free(w);
}

void async_preview_load(const char *path, int nrow)
{
	struct pv_load_work *w = malloc(sizeof(struct pv_load_work));
	w->path = strdup(path);
	w->nrow = nrow;
	tpool_add_work(async_tm, async_preview_load_worker, w);
}
