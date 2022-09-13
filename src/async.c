#include <ev.h>
#include <stdint.h>
#include <sys/sysinfo.h>
#include <errno.h>

#include "async.h"
#include "config.h"
#include "fm.h"
#include "hashtab.h"
#include "loader.h"
#include "log.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#define DIRCOUNT_THRESHOLD 200  // send batches of dircounts around every 200ms

typedef struct result_s {
  void (*callback)(void *, Lfm *);
  void (*destroy)(void *);
  struct result_s *next;
} Result;

typedef struct result_queue_s {
  Result *head;
  Result *tail;
  pthread_mutex_t mutex;
} ResultQueue;

struct async_watcher_data {
  Lfm *lfm;
  ResultQueue *queue;
};

static Result *resultqueue_get(ResultQueue *queue);

static ResultQueue async_results = {
  .head = NULL,
  .tail = NULL,
  .mutex = PTHREAD_MUTEX_INITIALIZER,
};

static tpool_t *async_tm = NULL;
static ev_async async_res_watcher;
static Hashtab *dircache = NULL;
static Lfm *lfm = NULL;


static void async_result_cb(EV_P_ ev_async *w, int revents)
{
  (void) revents;
  struct async_watcher_data *data = w->data;
  Result *res;

  pthread_mutex_lock(&data->queue->mutex);
  while ((res = resultqueue_get(data->queue))) {
    res->callback(res, data->lfm);
  }
  pthread_mutex_unlock(&data->queue->mutex);

  ev_idle_start(loop, &data->lfm->redraw_watcher);
}


void async_init(Lfm *_app)
{
  ev_async_init(&async_res_watcher, async_result_cb);
  ev_async_start(_app->loop, &async_res_watcher);

  lfm = _app;
  dircache = loader_hashtab();

  if (pthread_mutex_init(&async_results.mutex, NULL) != 0) {
    log_error("pthread_mutex_init: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  struct async_watcher_data *data = malloc(sizeof *data);
  data->lfm = _app;
  data->queue = &async_results;
  async_res_watcher.data = data;

  ev_async_send(EV_DEFAULT_ &async_res_watcher);

  const size_t nthreads = get_nprocs() + 1;
  async_tm = tpool_create(nthreads);
}


void async_deinit()
{
  tpool_wait(async_tm);
  tpool_destroy(async_tm);
  async_tm = NULL;

  Result *res;
  while ((res = resultqueue_get(&async_results))) {
    res->destroy(res);
  }
  pthread_mutex_destroy(&async_results.mutex);
  free(async_res_watcher.data);

  dircache = NULL;
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


static Result *resultqueue_get(ResultQueue *t)
{
  Result *res = t->head;

  if (!res) {
    return NULL;
  }

  t->head = res->next;
  res->next = NULL;
  if (t->tail == res) {
    t->tail = NULL;
  }

  return res;
}


static inline void enqueue_and_signal(Result *res)
{
  pthread_mutex_lock(&async_results.mutex);
  resultqueue_put(&async_results, res);
  pthread_mutex_unlock(&async_results.mutex);
  ev_async_send(EV_DEFAULT_ &async_res_watcher);
}

/* dir_check {{{ */


typedef struct dir_check_result_s {
  Result super;
  Dir *dir;
} DirCheckResult;


// TODO: maybe on slow devices it is better to compare mtimes here? 2021-11-12
// currently we could just schedule reload from the other thread
static void DirCheckResult_callback(void *p, Lfm *lfm)
{
  DirCheckResult *res = p;
  (void) lfm;
  loader_reload(res->dir);
  free(res);
}


static void DirCheckResult_destroy(void *p)
{
  DirCheckResult *res = p;
  free(res);
}


static inline DirCheckResult *DirCheckResult_create(Dir *dir)
{
  DirCheckResult *res = malloc(sizeof *res);
  res->super.callback = &DirCheckResult_callback;
  res->super.destroy = &DirCheckResult_destroy;
  res->super.next = NULL;
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

  DirCheckResult *res = DirCheckResult_create(work->dir);
  enqueue_and_signal((Result *) res);

cleanup:
  free(work);
}


void async_dir_check(Dir *dir)
{
  struct dir_check_work *work = malloc(sizeof *work);
  work->dir = dir;
  work->loadtime = dir->load_time;
  tpool_add_work(async_tm, async_dir_check_worker, work);
}


/* }}} */

/* dircount {{{ */

typedef struct dir_count_result_s {
  Result super;
  Dir *dir;
  struct dircount {
    File *file;
    uint32_t count;
  } *dircounts;
  bool last;
  uint32_t version;
} DirCountResult;


struct file_path {
  File *file;
  char *path;
};


static void DirCountResult_callback(void *p, Lfm *lfm)
{
  DirCountResult *res = p;
  /* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
  /* for now, just discard the dircount updates if any other update has been
   * applied in the meantime. This does not protect against the dir getting purged from
   * the cache.*/
  if (res->version == dircache->version && res->dir->updates <= 1)  {
    for (size_t i = 0; i < cvector_size(res->dircounts); i++) {
      file_dircount_set(res->dircounts[i].file, res->dircounts[i].count);
    }
    ui_redraw(&lfm->ui, REDRAW_FM);
    if (res->last) {
      res->dir->dircounts = true;
    }
  }
  cvector_free(res->dircounts);
  free(res);
}


static void DirCountResult_destroy(void *p)
{
  DirCountResult *res = p;
  cvector_free(res->dircounts);
  free(res);
}


static inline DirCountResult *DirCountResult_create(Dir *dir, struct dircount* files, uint32_t version, bool last)
{
  DirCountResult *res = malloc(sizeof *res);
  res->super.callback = &DirCountResult_callback;
  res->super.destroy = &DirCountResult_destroy;
  res->super.next = NULL;
  res->dir = dir;
  res->dircounts = files;
  res->last = last;
  res->version = version;
  return res;
}


// Not a worker function because we just call it from async_dir_load_worker
static void async_load_dircounts(Dir *dir, uint32_t version, uint32_t n, struct file_path *files)
{
  cvector_vector_type(struct dircount) counts = NULL;

  uint64_t latest = current_millis();

  /* TODO: we need to make sure that the original files/dir don't get freed (on 2022-01-15) */
  for (uint32_t i = 0; i < n; i++) {
    cvector_push_back(counts, ((struct dircount) {files[i].file, path_dircount(files[i].path)}));
    free(files[i].path);

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


typedef struct dir_update_result_s {
  Result super;
  Dir *dir;
  Dir *update;
  uint32_t version;
} DirUpdateResult;


static void DirUpdateResult_callback(void *p, Lfm *lfm)
{
  DirUpdateResult *res = p;
  if (res->version == dircache->version
      && res->dir->flatten_level == res->update->flatten_level) {
    dir_update_with(res->dir, res->update, lfm->fm.height, cfg.scrolloff);
    if (res->dir->visible) {
      fm_update_preview(&lfm->fm);
      ui_redraw(&lfm->ui, REDRAW_FM);
    }
  } else {
    dir_destroy(res->update);
  }
  free(res);
}


static void DirUpdateResult_destroy(void *p)
{
  DirUpdateResult *res = p;
  dir_destroy(res->update);
  free(res);
}


static inline DirUpdateResult *DirUpdateResult_create(Dir *dir, Dir *update, uint32_t version)
{
  DirUpdateResult *res = malloc(sizeof *res);
  res->super.callback = &DirUpdateResult_callback;
  res->super.destroy = &DirUpdateResult_destroy;
  res->super.next = NULL;
  res->dir = dir;
  res->update = update;
  res->version = version;
  return res;
}


struct dir_load_work {
  Dir *dir;
  char *path;
  bool dircounts;
  uint32_t level;
  uint32_t version;
};


static void async_dir_load_worker(void *arg)
{
  struct dir_load_work *work = arg;

  Dir *dir;
  if (work->level > 0) {
    dir = dir_load_flat(work->path, work->level, work->dircounts);
  } else {
    dir = dir_load(work->path, work->dircounts);
  }

  DirUpdateResult *res = DirUpdateResult_create(work->dir, dir, work->version);

  const uint32_t nfiles = res->update->length_all;
  struct file_path *files = NULL;
  if (!work->dircounts && nfiles > 0) {
    files = malloc(nfiles * sizeof *files);
    for (uint32_t i = 0; i < nfiles; i++) {
      files[i].file = res->update->files_all[i];
      files[i].path = strdup(res->update->files_all[i]->path);
    }
  }

  enqueue_and_signal((Result *) res);

  if (!work->dircounts && nfiles > 0) {
    async_load_dircounts(work->dir, work->version, nfiles, files);
  }

  free(work->path);
  free(work);
}

void async_dir_load(Dir *dir, bool dircounts)
{
  struct dir_load_work *work = malloc(sizeof *work);
  work->dir = dir;
  work->path = strdup(dir->path);
  work->dircounts = dircounts;
  work->level = dir->flatten_level;
  work->version = dircache->version;
  tpool_add_work(async_tm, async_dir_load_worker, work);
}


/* }}} */

/* preview_check {{{ */

typedef struct preview_check_result_s {
  Result super;
  char *path;
  int nrow;
} PreviewCheckResult;


static void PreviewCheckResult_callback(void *p, Lfm *lfm)
{
  PreviewCheckResult *res = p;
  (void) lfm;
  Preview *pv = ht_get(lfm->ui.preview.cache, res->path);
  if (pv) {
    async_preview_load(pv, res->nrow);
  }
  free(res->path);
  free(res);
}


static void PreviewCheckResult_destroy(void *p)
{
  PreviewCheckResult *res = p;
  free(res->path);
  free(res);
}


static inline PreviewCheckResult *PreviewCheckResult_create(char *path, int nrow)
{
  PreviewCheckResult *res = malloc(sizeof *res);
  res->super.callback = &PreviewCheckResult_callback;
  res->super.destroy = &PreviewCheckResult_destroy;
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
  PreviewCheckResult *res = PreviewCheckResult_create(work->path, work->nrow);
  enqueue_and_signal((Result *) res);

cleanup:
  free(work);
}


void async_preview_check(Preview *pv)
{
  struct preview_check_work *work = malloc(sizeof *work);
  work->path = strdup(pv->path);
  work->nrow = pv->nrow;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;
  tpool_add_work(async_tm, async_preview_check_worker, work);
}

/* }}} */

/* preview_load {{{ */

typedef struct preview_load_result_s {
  Result super;
  Preview *preview;
  Preview *update;
  uint32_t version;
} PreviewLoadResult;


static void PreviewLoadResult_callback(void *p, Lfm *lfm)
{
  PreviewLoadResult *res = p;
  // TODO: make this safer, preview.cache.version protects against dropped
  // caches only (on 2022-02-06)
  if (res->version == lfm->ui.preview.cache->version) {
    preview_update(res->preview, res->update);
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
  } else {
    preview_destroy(res->update);
  }
  free(res);
}


static void PreviewLoadResult_destroy(void *p)
{
  PreviewLoadResult *res = p;
  preview_destroy(res->update);
  free(res);
}


static inline PreviewLoadResult *PreviewLoadResult_create(Preview *preview, Preview *update, uint32_t version)
{
  PreviewLoadResult *res = malloc(sizeof *res);
  res->super.callback = PreviewLoadResult_callback;
  res->super.destroy = PreviewLoadResult_destroy;
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
  bool image_preview;
  uint32_t version;
};


static void async_preview_load_worker(void *arg)
{
  struct preview_load_work *work = arg;

  PreviewLoadResult *res = PreviewLoadResult_create(
      work->preview,
      preview_create_from_file(work->path, work->nrow, work->image_preview),
      work->version);
  enqueue_and_signal((Result *) res);

  free(work->path);
  free(work);
}


void async_preview_load(Preview *pv, uint32_t nrow)
{
  struct preview_load_work *work = malloc(sizeof *work);
  work->preview = pv;
  work->path = strdup(pv->path);
  work->nrow = nrow;
  work->version = lfm->ui.preview.cache->version;
  work->image_preview = preview_is_image_preview(pv);
  tpool_add_work(async_tm, async_preview_load_worker, work);
}

/* }}} */
