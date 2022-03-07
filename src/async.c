#include <stdint.h>
#include <sys/sysinfo.h>
#include <errno.h>

#include "async.h"
#include "cache.h"
#include "config.h"
#include "fm.h"
#include "log.h"
#include "ui.h"
#include "util.h"

#define DIRCOUNT_THRESHOLD 200 // send batches of dircounts around every 200ms

static tpool_t *async_tm = NULL;
static Cache *dircache = NULL;
static Cache *previewcache = NULL;

static ResultQueue async_results = {
	.head = NULL,
	.tail = NULL,
	.watcher = NULL,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

static void result_destroy(Result *res);

void async_init(App *app)
{
	dircache = &app->fm.dirs.cache;
	previewcache = &app->ui.preview.cache;

	if (pthread_mutex_init(&async_results.mutex, NULL) != 0) {
		log_error("pthread_mutex_init: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	async_results.watcher = &app->async_res_watcher;

	struct async_watcher_data *async_res_watcher_data = malloc(sizeof(*async_res_watcher_data));
	async_res_watcher_data->app = app;
	async_res_watcher_data->queue = &async_results;
	app->async_res_watcher.data = async_res_watcher_data;

	ev_async_send(EV_DEFAULT_ &app->async_res_watcher); /* results will arrive before the loop starts */

	const size_t nthreads = get_nprocs()+1;
	async_tm = tpool_create(nthreads);
}


void async_deinit()
{
	tpool_wait(async_tm);
	tpool_destroy(async_tm);
	async_tm = NULL;

	Result *res;
	while ((res = resultqueue_get(&async_results)))
		result_destroy(res);
	pthread_mutex_destroy(&async_results.mutex);
	free(async_results.watcher->data);

	dircache = NULL;
	previewcache = NULL;
}

/*
 * `process` is run on the main thread and should do whatever is necessary to process Result,
 * consuming it (freeing whatever resources remain).
 *
 * `destroy` shall completely free all resources of a Result.
 */
typedef struct Result_vtable {
	void (*process)(Result *, App *);
	void (*destroy)(Result *);
} Result_vtable;


typedef struct Result {
	Result_vtable *vtable;
	struct Result *next;
} Result;


void result_process(Result *res, App *app)
{
	res->vtable->process(res, app);
}


static void result_destroy(Result *res)
{
	res->vtable->destroy(res);
}


static void resultqueue_put(ResultQueue *t, Result *res)
{
	if (!t->head) {
		t->head = res;
		t->tail = res;
	} else {
		t->tail->next = res;
		t->tail = res;
	}
}


Result *resultqueue_get(ResultQueue *t)
{
	Result *res = t->head;

	if (!res)
		return NULL;

	t->head = res->next;
	res->next = NULL;
	if (t->tail == res)
		t->tail = NULL;

	return res;
}


static inline void enqueue_and_signal(Result *res)
{
	pthread_mutex_lock(&async_results.mutex);
	resultqueue_put(&async_results, res);
	pthread_mutex_unlock(&async_results.mutex);

	if (async_results.watcher) {
		ev_async_send(EV_DEFAULT_ async_results.watcher);
	}
}

/* dir_check {{{ */


typedef struct DirCheckResult {
	Result super;
	Dir *dir;
} DirCheckResult;


/* TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12 */
/* currently we could just schedule reload from the other thread */
static void DirCheckResult_process(DirCheckResult *res, App *app)
{
	(void) app;
	async_dir_load(res->dir, true);
	free(res);
}


static void DirCheckResult_destroy(DirCheckResult *res)
{
	free(res);
}


static Result_vtable res_dir_check_vtable = {
	(void (*)(Result *, App *)) &DirCheckResult_process,
	(void (*)(Result *)) &DirCheckResult_destroy,
};


static inline DirCheckResult *DirCheckResult_create(Dir *dir)
{
	DirCheckResult *res = malloc(sizeof(DirCheckResult));
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

	if (stat(work->dir->path, &statbuf) == -1)
		goto cleanup;

	if (statbuf.st_mtime <= work->loadtime)
		goto cleanup;

	DirCheckResult *res = DirCheckResult_create(work->dir);
	enqueue_and_signal((Result *) res);

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
typedef struct jDirCountResult {
	Result super;
	Dir *dir;
	struct dircount {
		File *file;
		uint16_t count;
	} *dircounts;
	bool last;
	uint8_t version;
} DirCountResult;


struct file_path {
	File *file;
	char *path;
};


static void DirCountResult_process(DirCountResult *res, App *app)
{
	/* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
	/* for now, just discard the dircount updates if any other update has been
	 * applied in the meantime. This does not protect against the dir getting purged from
	 * the cache.*/
	if (res->version == dircache->version && res->dir->updates <= 1)  {
		for (size_t i = 0; i < cvector_size(res->dircounts); i++)
			file_dircount_set(res->dircounts[i].file, res->dircounts[i].count);
		ui_redraw(&app->ui, REDRAW_FM);
		if (res->last)
			res->dir->dircounts = true;
	}
	cvector_free(res->dircounts);
	free(res);
}


static void DirCountResult_destroy(DirCountResult *res)
{
	cvector_free(res->dircounts);
	free(res);
}


static Result_vtable DirCountResult_vtable = {
	(void (*)(Result *, App *)) &DirCountResult_process,
	(void (*)(Result *)) &DirCountResult_destroy,
};


static inline DirCountResult *DirCountResult_create(Dir *dir, struct dircount* files, uint8_t version, bool last)
{
	DirCountResult *res = malloc(sizeof(DirCountResult));
	res->super.vtable = &DirCountResult_vtable;
	res->super.next = NULL;
	res->dir = dir;
	res->dircounts = files;
	res->last = last;
	res->version = version;
	return res;
}


// Not a worker function because we just call it from async_dir_load_worker
static void async_load_dircounts(Dir *dir, uint8_t version, uint16_t n, struct file_path *files)
{
	cvector_vector_type(struct dircount) counts = NULL;

	uint64_t latest = current_millis();

	/* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
	for (uint16_t i = 0; i < n; i++) {
		cvector_push_back(counts, ((struct dircount) {files[i].file, path_dircount(files[i].path)}));

		if (current_millis() - latest > DIRCOUNT_THRESHOLD) {
			DirCountResult *res = DirCountResult_create(dir, counts, version, false);
			enqueue_and_signal((Result *) res);

			counts = NULL;
			latest = current_millis();
		}
	}

	DirCountResult *res = DirCountResult_create(dir, counts, version, true);
	enqueue_and_signal((Result *) res);

	free(files);
}

/* }}} */

/* dir_update {{{ */


typedef struct DirUpdateResult {
	Result super;
	Dir *dir;
	Dir *update;
	uint8_t version;
} DirUpdateResult;


static void DirUpdateResult_process(DirUpdateResult *res, App *app)
{
	if (res->version == dircache->version
			&& res->dir->flatten_level == res->update->flatten_level) {
		dir_update_with(res->dir, res->update, app->fm.height, cfg.scrolloff);
		if (res->dir->visible) {
			fm_update_preview(&app->fm);
			ui_redraw(&app->ui, REDRAW_FM);
		}
	} else {
		dir_destroy(res->update);
	}
	free(res);
}


static void DirUpdateResult_destroy(DirUpdateResult *res)
{
	dir_destroy(res->update);
	free(res);
}


static Result_vtable DirUpdateResult_vtable = {
	(void (*)(Result *, App *)) &DirUpdateResult_process,
	(void (*)(Result *)) &DirUpdateResult_destroy,
};


static inline DirUpdateResult *DirUpdateResult_create(Dir *dir, Dir *update, uint8_t version)
{
	DirUpdateResult *res = malloc(sizeof(DirUpdateResult));
	res->super.vtable = &DirUpdateResult_vtable;
	res->super.next = NULL;
	res->dir = dir;
	res->update = update;
	res->version = version;
	return res;
}


struct dir_load_work {
	Dir *dir;
	char *path;
	uint16_t delay;
	bool dircounts;
	uint8_t level;
	uint8_t version;
};


static void async_dir_load_worker(void *arg)
{
	struct dir_load_work *work = arg;

	if (work->delay > 0)
		msleep(work->delay);

	Dir *dir;
	if (work->level > 0)
		dir = dir_load_flat(work->path, work->level, work->dircounts);
	else
		dir = dir_load(work->path, work->dircounts);

	DirUpdateResult *res = DirUpdateResult_create(work->dir, dir, work->version);

	const uint16_t nfiles = res->update->length_all;
	struct file_path *files = NULL;
	if (!work->dircounts && nfiles > 0) {
		files = malloc(nfiles * sizeof(struct file_path));
		for (uint16_t i = 0; i < nfiles; i++) {
			files[i].file = res->update->files_all[i];
			files[i].path = strdup(res->update->files_all[i]->path);
		}
	}

	enqueue_and_signal((Result *) res);

	if (!work->dircounts && nfiles > 0)
		async_load_dircounts(work->dir, work->version, nfiles, files);

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
	work->level = dir->flatten_level;
	work->version = dircache->version;
	tpool_add_work(async_tm, async_dir_load_worker, work);
}


/* }}} */

/* preview_check {{{ */

struct PreviewCheckResult {
	Result super;
	char *path;
	int nrow;
};


static void PreviewCheckResult_process(struct PreviewCheckResult *res, App *app)
{
	(void) app;
	Preview *pv = cache_find(&app->ui.preview.cache, res->path);
	if (pv)
		async_preview_load(pv, res->nrow);
	free(res->path);
	free(res);
}


static void PreviewCheckResult_destroy(struct PreviewCheckResult *res)
{
	free(res->path);
	free(res);
}


static Result_vtable PrevewCheckResult_vtable = {
	(void (*)(Result *, App *)) &PreviewCheckResult_process,
	(void (*)(Result *)) &PreviewCheckResult_destroy,
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
	uint64_t loadtime;
};


static void async_preview_check_worker(void *arg)
{
	struct preview_check_work *work = arg;
	struct stat statbuf;

	if (stat(work->path, &statbuf) == -1) {
		free(work->path);
		goto cleanup;
	}

	/* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
	if (statbuf.st_mtime <= work->mtime && statbuf.st_mtime <= (long) (work->loadtime / 1000 - 1)) {
		free(work->path);
		goto cleanup;
	}

	// takes ownership of work->path
	struct PreviewCheckResult *res = PreviewCheckResult_create(work->path, work->nrow);
	enqueue_and_signal((Result *) res);

cleanup:
	free(work);
}


void async_preview_check(Preview *pv)
{
	struct preview_check_work *work = malloc(sizeof(struct preview_check_work));
	work->path = strdup(pv->path);
	work->nrow = pv->nrow;
	work->mtime = pv->mtime;
	work->loadtime = pv->loadtime;
	tpool_add_work(async_tm, async_preview_check_worker, work);
}

/* }}} */

/* preview_load {{{ */

typedef struct PreviewLoadResult {
	Result super;
	Preview *preview;
	Preview *update;
	uint8_t version;
} PreviewLoadResult;


static void PreviewLoadResult_process(PreviewLoadResult *res, App *app)
{
	// TODO: make this safer, previewcache.version protects against dropped
	// caches only (on 2022-02-06)
	if (res->version == previewcache->version) {
		preview_update_with(res->preview, res->update);
		ui_redraw(&app->ui, REDRAW_PREVIEW);
	} else {
		preview_destroy(res->update);
	}
	free(res);
}


static void PreviewLoadResult_destroy(PreviewLoadResult *res)
{
	preview_destroy(res->update);
	free(res);
}


static Result_vtable PreviewLoadResult_vtable = {
	(void (*)(Result *, App *)) &PreviewLoadResult_process,
	(void (*)(Result *)) &PreviewLoadResult_destroy,
};


static inline PreviewLoadResult *PreviewLoadResult_create(Preview *preview, Preview *update, uint8_t version)
{
	PreviewLoadResult *res = malloc(sizeof(PreviewLoadResult));
	res->super.vtable = &PreviewLoadResult_vtable;
	res->super.next = NULL;
	res->preview = preview;
	res->update = update;
	res->version = version;
	return res;
}


struct preview_load_work {
	char *path;
	Preview *preview;
	int nrow;
	uint8_t version;
};


static void async_preview_load_worker(void *arg)
{
	struct preview_load_work *work = arg;

	PreviewLoadResult *res = PreviewLoadResult_create(
			work->preview,
			preview_create_from_file(work->path, work->nrow),
			work->version);
	enqueue_and_signal((Result *) res);

	free(work->path);
	free(work);
}


void async_preview_load(Preview *pv, uint16_t nrow)
{
	struct preview_load_work *work = malloc(sizeof(struct preview_load_work));
	work->preview = pv;
	work->path = strdup(pv->path);
	work->nrow = nrow;
	work->version = previewcache->version;
	tpool_add_work(async_tm, async_preview_load_worker, work);
}

/* }}} */
