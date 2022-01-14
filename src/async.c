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

struct res_vtable {
	void (*callback)(struct res_t *, struct app_t *);
	void (*destroy)(struct res_t *);
};

struct res_t {
	struct res_vtable *vtable;
	struct res_t *next;
};

void res_callback(struct res_t *res, app_t *app)
{
	res->vtable->callback(res, app);
}

static void res_destroy(struct res_t *res)
{
	res->vtable->destroy(res);
}

/* result queue {{{ */
void queue_put(resq_t *queue, struct res_t *res)
{
	if (queue->head == NULL) {
		queue->head = res;
		queue->tail = res;
	} else {
		queue->tail->next = res;
		queue->tail = res;
	}
}

struct res_t *queue_get(resq_t *queue)
{
	struct res_t *res = queue->head;

	if (res == NULL) {
		return NULL;
	}

	queue->head = res->next;
	res->next = NULL;
	if (queue->tail == res) {
		queue->tail = NULL;
	}

	return res;
}

void queue_deinit(resq_t *queue)
{
	struct res_t *res;
	while ((res = queue_get(queue)) != NULL) {
		res_destroy(res);
	}
}
/* }}} */

/* dir_check {{{ */

struct res_dir_check {
	struct res_t super;
	Dir *dir;
};

/* TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12 */
/* currently we could just schedule reload from the other thread */
static void res_dir_check_callback(struct res_dir_check *result, app_t *app)
{
	(void) app;
	async_dir_load(result->dir, true);
	free(result);
}

static void res_dir_check_destroy(struct res_dir_check *result) {
	free(result);
}

static struct res_vtable res_dir_check_vtable = {
	(void (*)(struct res_t *, app_t *)) &res_dir_check_callback,
	(void (*)(struct res_t *)) &res_dir_check_destroy,
};

static inline struct res_dir_check *res_dir_check_create(Dir *dir)
{
	struct res_dir_check *res = malloc(sizeof(struct res_dir_check));
	res->super.vtable = &res_dir_check_vtable;
	res->dir = dir;
	return res;
}

struct dir_check_work {
	Dir *dir;
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

	struct res_dir_check *res = res_dir_check_create(w->dir);

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, (struct res_t *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_dir_check(Dir *dir)
{
	struct dir_check_work *w = malloc(sizeof(struct dir_check_work));
	w->dir = dir;
	w->loadtime = dir->load_time;
	tpool_add_work(async_tm, async_dir_check_worker, w);
}

/* }}} */

/* dir_update {{{ */

struct res_dir_update {
	struct res_t super;
	Dir *dir;
	Dir *update;
};

static void res_dir_update_callback(struct res_dir_update *result, app_t *app)
{
	app->ui.redraw.fm |= fm_update_dir(&app->fm, result->dir, result->update);
	free(result);
}

static void res_dir_update_destroy(struct res_dir_update *result)
{
	dir_destroy(result->dir);
	free(result);
}

static struct res_vtable res_dir_update_vtable = {
	(void (*)(struct res_t *, app_t *)) &res_dir_update_callback,
	(void (*)(struct res_t *)) &res_dir_update_destroy,
};

static inline struct res_dir_update *res_dir_update_create(Dir *dir, Dir *update)
{
	struct res_dir_update *res = malloc(sizeof(struct res_dir_update));
	res->super.vtable = &res_dir_update_vtable;
	res->super.next = NULL;
	res->dir = dir;
	res->update = update;
	return res;
}

struct dir_load_work {
	Dir *dir;
	char *path;
	int delay;
	bool dircounts;
};

static void async_dir_load_worker(void *arg)
{
	struct dir_load_work *w = arg;
	if (w->delay > 0) {
		msleep(w->delay);
	}

	struct res_dir_update *res = res_dir_update_create(w->dir, dir_load(w->path, w->dircounts));

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, (struct res_t *) res);
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

void async_dir_load_delayed(Dir *dir, bool dircounts, int delay /* millis */)
{
	struct dir_load_work *w = malloc(sizeof(struct dir_load_work));
	w->dir = dir;
	w->path = strdup(dir->path);
	w->delay = delay;
	w->dircounts = dircounts;
	tpool_add_work(async_tm, async_dir_load_worker, w);
}

/* }}} */

/* preview_check {{{ */

struct res_preview_check {
	struct res_t super;
	char *path;
	int nrow;
};

static void res_preview_check_callback(struct res_preview_check *result, app_t *app)
{
	(void) app;
	async_preview_load(result->path, result->nrow);
	free(result->path);
	free(result);
}

static void res_preview_check_destroy(struct res_preview_check *result)
{
	free(result->path);
	free(result);
}

static struct res_vtable res_preview_check_vtable = {
	(void (*)(struct res_t *, app_t *)) &res_preview_check_callback,
	(void (*)(struct res_t *)) &res_preview_check_destroy,
};

static inline struct res_preview_check *res_preview_check_create(char *path, int nrow)
{
	struct res_preview_check *res = malloc(sizeof(struct res_preview_check));
	res->super.vtable = &res_preview_check_vtable;
	res->super.next = NULL;
	res->path = path;
	res->nrow = nrow;
	return res;
}

struct preview_check_work {
	char *path;
	int nrow;
	time_t mtime;
};

static void async_preview_check_worker(void *arg)
{
	struct preview_check_work *w = arg;
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

	struct res_preview_check *res = res_preview_check_create(w->path, w->nrow);

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, (struct res_t *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_preview_check(preview_t *pv)
{
	struct preview_check_work *w = malloc(sizeof(struct preview_check_work));
	w->path = strdup(pv->path);
	w->nrow = pv->nrow;
	w->mtime = pv->mtime;
	tpool_add_work(async_tm, async_preview_check_worker, w);
}

/* }}} */

/* preview_load {{{ */

struct res_preview_load {
	struct res_t super;
	preview_t *preview;
};

static void res_preview_load_callback(struct res_preview_load *result, app_t *app)
{
	app->ui.redraw.preview |= ui_insert_preview(&app->ui, result->preview);
	free(result);
}

static void res_preview_load_deinit(struct res_preview_load *result)
{
	preview_free(result->preview);
	free(result);
}

static struct res_vtable res_preview_load_vtable = {
	(void (*)(struct res_t *, app_t *)) &res_preview_load_callback,
	(void (*)(struct res_t *)) &res_preview_load_deinit,
};

static inline struct res_preview_load *res_preview_load_create(preview_t *preview)
{
	struct res_preview_load *res = malloc(sizeof(struct res_preview_load));
	res->super.vtable = &res_preview_load_vtable;
	res->super.next = NULL;
	res->preview = preview;
	return res;
}

struct preview_load_work {
	char *path;
	int nrow;
};

static void async_preview_load_worker(void *arg)
{
	struct preview_load_work *w = (struct preview_load_work*) arg;

	struct res_preview_load *res = res_preview_load_create(preview_new_from_file(w->path, w->nrow));

	pthread_mutex_lock(&async_results.mutex);
	queue_put(&async_results, (struct res_t *) res);
	pthread_mutex_unlock(&async_results.mutex);
	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
	free(w->path);
	free(w);
}

void async_preview_load(const char *path, int nrow)
{
	struct preview_load_work *w = malloc(sizeof(struct preview_load_work));
	w->path = strdup(path);
	w->nrow = nrow;
	tpool_add_work(async_tm, async_preview_load_worker, w);
}
/* }}} */
