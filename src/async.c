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

#define DIRCOUNT_THRESHOLD 200 /* in ms */

tpool_t *async_tm;

ResultQueue async_results = {
	.head = NULL,
	.tail = NULL,
	.watcher = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

/*
 * `callback` is run on the main thread and should do whatever is necessary to process Result,
 * consuming it (freeing whatever resources remain).
 *
 * `destroy` shall completely free all resources of a Result.
 */
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
	while ((res = resultqueue_get(t)) != NULL) {
		result_destroy(res);
	}
}

static void resultqueue_put(T *t, struct Result *res)
{
	if (t->head == NULL) {
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

	if (res == NULL) {
		return NULL;
	}

	t->head = res->next;
	res->next = NULL;
	if (t->tail == res) {
		t->tail = NULL;
	}

	return res;
}

#undef T

/* }}} */

static inline void enqueue_and_signal(struct Result *res)
{
	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
}

/* dir_check {{{ */

struct DirCheckResult {
	struct Result super;
	Dir *dir;
};

/* TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12 */
/* currently we could just schedule reload from the other thread */
static void DirCheckResult_callback(struct DirCheckResult *res, App *app)
{
	(void) app;
	async_dir_load(res->dir, true);
	free(res);
}

static void DirCheckResult_destroy(struct DirCheckResult *res)
{
	free(res);
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
	struct dir_check_work *work = arg;
	struct stat statbuf;

	if (stat(work->dir->path, &statbuf) == -1) {
		goto cleanup;
	}

	if (statbuf.st_mtime <= work->loadtime) {
		goto cleanup;
	}

	struct DirCheckResult *res = DirCheckResult_create(work->dir);

	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, (struct Result *) res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher != NULL) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}

cleanup:
	free(work);
}

void async_dir_check(Dir *dir)
{
	struct dir_check_work *work = malloc(sizeof(struct dir_check_work));
	work->dir = dir;
	work->loadtime = dir->load_time;
	tpool_add_work(async_tm, async_dir_check_worker, work);
}

/* }}} */

/* dircount {{{ */

/*
 * TODO: (on 2022-01-15)
 * Counting contents of directories asyncronuously relies on the original directory
 * and its files to exist during the whole process. If another dir update is somehow
 * faster and the update gets applied, the old files get deleted (has not happened so far).
 * If the dir gets deleted, either due to drop_caches or because it is purged from the cache,
 * the files also get deleted.
 *
 * we could use the (currently useless) Dir.dircounts variable and also remove drop_caches.
 */
struct DirCountResult {
	struct Result super;
	Dir *dir;
	struct dircount {
		File *file;
		uint16_t count;
	} *dircounts;
	bool last;
};

static void DirCountResult_callback(struct DirCountResult *res, App *app)
{
	/* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
	for (size_t i = 0; i < cvector_size(res->dircounts); i++) {
		file_dircount_set(res->dircounts[i].file, res->dircounts[i].count);
	}
	app->ui.redraw.fm = 1;
	if (res->last) {
		res->dir->dircounts = true;
	}
	cvector_free(res->dircounts);
	free(res);
}

static void DirCountResult_destroy(struct DirCountResult *res)
{
	cvector_free(res->dircounts);
	free(res);
}

static struct Result_vtable DirCountResult_vtable = {
	(void (*)(struct Result *, App *)) &DirCountResult_callback,
	(void (*)(struct Result *)) &DirCountResult_destroy,
};

static inline struct DirCountResult *DirCountResult_create(Dir *dir, struct dircount* files, bool last)
{
	struct DirCountResult *res = malloc(sizeof(struct DirCountResult));
	res->super.vtable = &DirCountResult_vtable;
	res->super.next = NULL;
	res->dir = dir;
	res->dircounts = files;
	res->last = last;
	return res;
}

// Not a worker function because we just call it from async_dir_load_worker
static void async_load_dircounts(Dir *dir, uint16_t n, File *files[n])
{
	cvector_vector_type(struct dircount) counts = NULL;

	uint64_t latest = current_millis();

	/* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
	for (uint16_t i = 0; i < n; i++) {
		cvector_push_back(counts, ((struct dircount) {files[i], file_dircount_load(files[i])}));

		if (current_millis() - latest > DIRCOUNT_THRESHOLD) {
			struct DirCountResult *res = DirCountResult_create(dir, counts, false);
			enqueue_and_signal((struct Result *) res);

			counts = NULL;
			latest = current_millis();
		}
	}

	struct DirCountResult *res = DirCountResult_create(dir, counts, true);
	enqueue_and_signal((struct Result *) res);

	free(files);
}

/* }}} */

/* dir_update {{{ */

struct DirUpdateResult {
	struct Result super;
	Dir *dir;
	Dir *update;
};

static void DirUpdateResult_callback(struct DirUpdateResult *res, App *app)
{
	app->ui.redraw.fm |= fm_update_dir(&app->fm, res->dir, res->update);
	free(res);
}

static void DirUpdateResult_destroy(struct DirUpdateResult *res)
{
	dir_destroy(res->dir);
	free(res);
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
	uint16_t delay;
	bool dircounts;
};

static void async_dir_load_worker(void *arg)
{
	struct dir_load_work *work = arg;

	if (work->delay > 0) {
		msleep(work->delay);
	}

	struct DirUpdateResult *res = DirUpdateResult_create(work->dir, dir_load(work->path, work->dircounts));

	const uint16_t nfiles = res->update->length_all;
	File **files = NULL;
	if (!work->dircounts && nfiles > 0) {
		files = malloc(sizeof(File *) * nfiles);
		memcpy(files, res->update->files_all, sizeof(File *) * nfiles);
	}

	enqueue_and_signal((struct Result *) res);

	if (!work->dircounts && nfiles > 0) {
		async_load_dircounts(work->dir, nfiles, files);
	}

	free(work->path);
	free(work);
}

void async_dir_load_delayed(Dir *dir, bool dircounts, uint16_t delay /* millis */)
{
	struct dir_load_work *work = malloc(sizeof(struct dir_load_work));
	work->dir = dir;
	work->path = strdup(dir->path);
	work->delay = delay;
	work->dircounts = dircounts;
	tpool_add_work(async_tm, async_dir_load_worker, work);
}

/* }}} */

/* preview_check {{{ */

struct PreviewCheckResult {
	struct Result super;
	char *path;
	int nrow;
};

static void PreviewCheckResult_callback(struct PreviewCheckResult *res, App *app)
{
	(void) app;
	async_preview_load(res->path, res->nrow);
	free(res->path);
	free(res);
}

static void PreviewCheckResult_destroy(struct PreviewCheckResult *res)
{
	free(res->path);
	free(res);
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
	struct preview_check_work *work = arg;
	struct stat statbuf;

	if (stat(work->path, &statbuf) == -1) {
		free(work->path);
		goto cleanup;
	}

	if (statbuf.st_mtime <= work->mtime) {
		free(work->path);
		goto cleanup;
	}

	// takes ownership of work->path
	struct PreviewCheckResult *res = PreviewCheckResult_create(work->path, work->nrow);
	enqueue_and_signal((struct Result *) res);

cleanup:
	free(work);
}

void async_preview_check(Preview *pv)
{
	struct preview_check_work *work = malloc(sizeof(struct preview_check_work));
	work->path = strdup(pv->path);
	work->nrow = pv->nrow;
	work->mtime = pv->mtime;
	tpool_add_work(async_tm, async_preview_check_worker, work);
}

/* }}} */

/* preview_load {{{ */

struct PreviewLoadResult {
	struct Result super;
	Preview *preview;
};

static void PreviewLoadResult_callback(struct PreviewLoadResult *res, App *app)
{
	app->ui.redraw.preview |= ui_insert_preview(&app->ui, res->preview);
	free(res);
}

static void PreviewLoadResult_destroy(struct PreviewLoadResult *res)
{
	preview_destroy(res->preview);
	free(res);
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
	struct preview_load_work *work = arg;

	struct PreviewLoadResult *res = PreviewLoadResult_create(
			preview_create_from_file(work->path, work->nrow));
	enqueue_and_signal((struct Result *) res);

	free(work->path);
	free(work);
}

void async_preview_load(const char *path, uint16_t nrow)
{
	struct preview_load_work *work = malloc(sizeof(struct preview_load_work));
	work->path = strdup(path);
	work->nrow = nrow;
	tpool_add_work(async_tm, async_preview_load_worker, work);
}

/* }}} */
