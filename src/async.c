#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "app.h"
#include "async.h"
#include "dir.h"
#include "fm.h"
#include "log.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

tpool_t *async_tm;

ResultQueue async_results = {
	.head = NULL,
	.tail = NULL,
	.watcher = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

struct Result_vtable {
	void (*callback)(struct Result *, App *);
	void (*destroy)(struct Result *);
};

struct Result {
	struct Result_vtable *vtable;
	struct Result *next;
};

void result_callback(struct Result *res, App *app)
{
	res->vtable->callback(res, app);
}

static void result_destroy(struct Result *res)
{
	res->vtable->destroy(res);
}

/* result queue {{{ */

#define T ResultQueue

void resultqueue_init(T *t, ev_async *watcher)
{
	t->watcher = watcher;
}

void resultqueue_deinit(T *t)
{
	struct Result *res;
	while ((res = resultqueue_get(t)))
		result_destroy(res);
}

static void resultqueue_put(T *t, struct Result *res)
{
	if (!t->head) {
		t->head = res;
		t->tail = res;
	} else {
		t->tail->next = res;
		t->tail = res;
	}
}

struct Result *resultqueue_get(T *t)
{
	struct Result *res = t->head;

	if (!res)
		return NULL;

	t->head = res->next;
	res->next = NULL;
	if (t->tail == res)
		t->tail = NULL;

	return res;
}

#undef T

/* }}} */

/* dir_check {{{ */

struct DirCheckResult {
	struct Result super;
	Dir *dir;
};

/* TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12 */
/* currently we could just schedule reload from the other thread */
static void DirCheckResult_callback(struct DirCheckResult *result, App *app)
{
	(void) app;
	async_dir_load(result->dir, true);
	free(result);
}

static void DirCheckResult_destroy(struct DirCheckResult *result)
{
	free(result);
}

static struct Result_vtable res_dir_check_vtable = {
	(void (*)(struct Result *, App *)) &DirCheckResult_callback,
	(void (*)(struct Result *)) &DirCheckResult_destroy,
};

static inline struct DirCheckResult *DirCheckResult_create(Dir *dir)
{
	struct DirCheckResult *res = malloc(sizeof(struct DirCheckResult));
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

	struct DirCheckResult *res = DirCheckResult_create(w->dir);

	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, (struct Result *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
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

struct DirUpdateResult {
	struct Result super;
	Dir *dir;
	Dir *update;
};

static void DirUpdateResult_callback(struct DirUpdateResult *result, App *app)
{
	app->ui.redraw.fm |= fm_update_dir(&app->fm, result->dir, result->update);
	free(result);
}

static void DirUpdateResult_destroy(struct DirUpdateResult *result)
{
	dir_destroy(result->dir);
	free(result);
}

static struct Result_vtable DirUpdateResult_vtable = {
	(void (*)(struct Result *, App *)) &DirUpdateResult_callback,
	(void (*)(struct Result *)) &DirUpdateResult_destroy,
};

static inline struct DirUpdateResult *DirUpdateResult_create(Dir *dir, Dir *update)
{
	struct DirUpdateResult *res = malloc(sizeof(struct DirUpdateResult));
	res->super.vtable = &DirUpdateResult_vtable;
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

	struct DirUpdateResult *res = DirUpdateResult_create(w->dir, dir_load(w->path, w->dircounts));

	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, (struct Result *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
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

void async_dir_load_delayed(Dir *dir, bool dircounts, uint16_t delay /* millis */)
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

struct PreviewCheckResult {
	struct Result super;
	char *path;
	int nrow;
};

static void PreviewCheckResult_callback(struct PreviewCheckResult *result, App *app)
{
	(void) app;
	async_preview_load(result->path, result->nrow);
	free(result->path);
	free(result);
}

static void PreviewCheckResult_destroy(struct PreviewCheckResult *result)
{
	free(result->path);
	free(result);
}

static struct Result_vtable PrevewCheckResult_vtable = {
	(void (*)(struct Result *, App *)) &PreviewCheckResult_callback,
	(void (*)(struct Result *)) &PreviewCheckResult_destroy,
};

static inline struct PreviewCheckResult *PreviewCheckResult_create(char *path, int nrow)
{
	struct PreviewCheckResult *res = malloc(sizeof(struct PreviewCheckResult));
	res->super.vtable = &PrevewCheckResult_vtable;
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

	struct PreviewCheckResult *res = PreviewCheckResult_create(w->path, w->nrow);

	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, (struct Result *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

	free(w);
}

void async_preview_check(Preview *pv)
{
	struct preview_check_work *w = malloc(sizeof(struct preview_check_work));
	w->path = strdup(pv->path);
	w->nrow = pv->nrow;
	w->mtime = pv->mtime;
	tpool_add_work(async_tm, async_preview_check_worker, w);
}

/* }}} */

/* preview_load {{{ */

struct PreviewLoadResult {
	struct Result super;
	Preview *preview;
};

static void PreviewLoadResult_callback(struct PreviewLoadResult *result, App *app)
{
	app->ui.redraw.preview |= ui_insert_preview(&app->ui, result->preview);
	free(result);
}

static void PreviewLoadResult_destroy(struct PreviewLoadResult *result)
{
	preview_destroy(result->preview);
	free(result);
}

static struct Result_vtable PreviewLoadResult_vtable = {
	(void (*)(struct Result *, App *)) &PreviewLoadResult_callback,
	(void (*)(struct Result *)) &PreviewLoadResult_destroy,
};

static inline struct PreviewLoadResult *PreviewLoadResult_create(Preview *preview)
{
	struct PreviewLoadResult *res = malloc(sizeof(struct PreviewLoadResult));
	res->super.vtable = &PreviewLoadResult_vtable;
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

	struct PreviewLoadResult *res = PreviewLoadResult_create(preview_create_from_file(w->path, w->nrow));

	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, (struct Result *) res);
	pthread_mutex_unlock(&async_results.mutex);
	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
	free(w->path);
	free(w);
}

void async_preview_load(const char *path, uint16_t nrow)
{
	struct preview_load_work *w = malloc(sizeof(struct preview_load_work));
	w->path = strdup(path);
	w->nrow = nrow;
	tpool_add_work(async_tm, async_preview_load_worker, w);
}
/* }}} */
