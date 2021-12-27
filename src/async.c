#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "async.h"
#include "dir.h"
#include "fm.h"
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
		if (result.free)
			result.free(&result);
	}
}

struct dir_check_work {
	dir_t *dir;
	time_t loadtime;
};

static void cb_dir_check(struct res_t *result, app_t *app)
{
	/* TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12 */
	/* currently we could just schedule reload from the other thread */
	(void) app;
	async_dir_load(result->dir, true);
}

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
		.cb = cb_dir_check,
		.free = NULL,
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
	char *path;
	int delay;
	bool dircounts;
};

static void cb_dir_update(struct res_t *result, app_t *app)
{
	app->ui.redraw.fm |= fm_update_dir(&app->fm, result->dir, result->update);
}

static void free_dir_update(struct res_t *result)
{
	dir_free(result->dir);
}

static void async_dir_load_worker(void *arg)
{
	struct dir_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}
	res_t r = {
		.cb = cb_dir_update,
		.free = free_dir_update,
		.dir = w->dir,
		.update = dir_load(w->path, w->dircounts)
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

	free(w->path);
	free(w);
}

void async_dir_load_delayed(dir_t *dir, bool dircounts, int delay /* millis */)
{
	struct dir_work *w = malloc(sizeof(struct dir_work));
	w->dir = dir;
	w->path = strdup(dir->path);
	w->delay = delay;
	w->dircounts = dircounts;
	tpool_add_work(async_tm, async_dir_load_worker, w);
}

struct pv_check_work {
	char *path;
	int nrow;
	time_t mtime;
};

static void cb_preview_check(struct res_t *result, app_t *app)
{
	(void) app;
	async_preview_load(result->path, result->nrow);
	free(result->path);
}

static void free_preview_check(struct res_t *result)
{
	free(result->path);
}

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
		.cb = cb_preview_check,
		.free = free_preview_check,
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

static void cb_preview(struct res_t *result, app_t *app)
{
	app->ui.redraw.preview |= ui_insert_preview(&app->ui, result->preview);
}

static void free_preview(struct res_t *result)
{
	preview_free(result->preview);
}

static void async_preview_load_worker(void *arg)
{
	struct pv_load_work *w = (struct pv_load_work*) arg;
	res_t r = {
		.cb = cb_preview,
		.free = free_preview,
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
