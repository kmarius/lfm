#include <errno.h>
#include <ev.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/sysinfo.h>

#include "async.h"
#include "config.h"
#include "dir.h"
#include "file.h"
#include "fm.h"
#include "hashtab.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "notify.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

#define DIRCOUNT_THRESHOLD 200  // send batches of dircounts around every 200ms

typedef struct result_s {
  void (*callback)(void *, Lfm *);
  void (*destroy)(void *);
  struct result_s *next;
} Result;


static Result *resultqueue_get(struct result_queue *queue);

static void async_result_cb(EV_P_ ev_async *w, int revents)
{
  (void) revents;
  Async *async = w->data;
  Result *res;

  pthread_mutex_lock(&async->queue.mutex);
  while ((res = resultqueue_get(&async->queue))) {
    res->callback(res, async->lfm);
  }
  pthread_mutex_unlock(&async->queue.mutex);

  ev_idle_start(loop, &async->lfm->redraw_watcher);
}


void async_init(Async *async, Lfm *lfm)
{
  async->lfm = lfm;
  async->queue.head = NULL;
  async->queue.tail = NULL;
  pthread_mutex_init(&async->queue.mutex, NULL);

  ev_async_init(&async->result_watcher, async_result_cb);
  ev_async_start(lfm->loop, &async->result_watcher);

  if (pthread_mutex_init(&async->queue.mutex, NULL) != 0) {
    log_error("pthread_mutex_init: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  async->result_watcher.data = async;

  ev_async_send(EV_DEFAULT_ &async->result_watcher);

  const size_t nthreads = get_nprocs() + 1;
  async->tpool = tpool_create(nthreads);
}


void async_deinit(Async *async)
{
  tpool_wait(async->tpool);
  tpool_destroy(async->tpool);

  Result *res;
  while ((res = resultqueue_get(&async->queue))) {
    res->destroy(res);
  }
  pthread_mutex_destroy(&async->queue.mutex);
}


static void resultqueue_put(struct result_queue *t, Result *res)
{
  if (!t->head) {
    t->head = res;
    t->tail = res;
  } else {
    t->tail->next = res;
    t->tail = res;
  }
}


static Result *resultqueue_get(struct result_queue *t)
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


static inline void enqueue_and_signal(Async *async, Result *res)
{
  pthread_mutex_lock(&async->queue.mutex);
  resultqueue_put(&async->queue, res);
  pthread_mutex_unlock(&async->queue.mutex);
  ev_async_send(EV_DEFAULT_ &async->result_watcher);
}

/* dir_check {{{ */


typedef struct dir_check_result_s {
  Result super;
  Dir *dir;
} DirCheckResult;


// TODO: maybe on slow devices it is better to compare mtimes *here*? 2021-11-12
// currently we could just schedule reload from the other thread
static void DirCheckResult_callback(void *p, Lfm *lfm)
{
  DirCheckResult *res = p;
  (void) lfm;
  loader_dir_reload(&lfm->loader, res->dir);
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
  Async *async;
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
  enqueue_and_signal(work->async, (Result *) res);

cleanup:
  free(work);
}


void async_dir_check(Async *async, Dir *dir)
{
  struct dir_check_work *work = malloc(sizeof *work);
  work->async = async;
  work->dir = dir;
  work->loadtime = dir->load_time;
  tpool_add_work(async->tpool, async_dir_check_worker, work);
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
  // discard if any other update has been applied in the meantime
  if (res->version == lfm->loader.dir_cache_version && res->dir->updates <= 1)  {
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
static void async_load_dircounts(Async *async, Dir *dir, uint32_t version, uint32_t n, struct file_path *files)
{
  cvector_vector_type(struct dircount) counts = NULL;

  uint64_t latest = current_millis();

  for (uint32_t i = 0; i < n; i++) {
    cvector_push_back(counts, ((struct dircount) {files[i].file, path_dircount(files[i].path)}));
    free(files[i].path);

    if (current_millis() - latest > DIRCOUNT_THRESHOLD) {
      DirCountResult *res = DirCountResult_create(dir, counts, version, false);
      enqueue_and_signal(async, (Result *) res);

      counts = NULL;
      latest = current_millis();
    }
  }

  DirCountResult *res = DirCountResult_create(dir, counts, version, true);
  enqueue_and_signal(async, (Result *) res);

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
  if (res->version == lfm->loader.dir_cache_version
      && res->dir->flatten_level == res->update->flatten_level) {
    if (res->update->length_all != res->dir->length_all) {
      // try update parent dircount
      const char *parent_path = dir_parent_path(res->dir);
      if (parent_path) {
        Dir *parent = ht_get(lfm->loader.dir_cache, parent_path);
        if (parent) {
          cvector_foreach(File *file, parent->files_all) {
            if (streq(file_name(file), res->dir->name)) {
              file_dircount_set(file, res->update->length_all);
              break;
            }
          }
        }
      }
    }
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
  Async *async;
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

  enqueue_and_signal(work->async, (Result *) res);

  if (!work->dircounts && nfiles > 0) {
    async_load_dircounts(work->async, work->dir, work->version, nfiles, files);
  }

  free(work->path);
  free(work);
}

void async_dir_load(Async *async, Dir *dir, bool dircounts)
{
  struct dir_load_work *work = malloc(sizeof *work);
  work->async = async;
  work->dir = dir;
  work->path = strdup(dir->path);
  work->dircounts = dircounts;
  work->level = dir->flatten_level;
  work->version = async->lfm->loader.dir_cache_version;
  tpool_add_work(async->tpool, async_dir_load_worker, work);
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
  Preview *pv = ht_get(lfm->loader.preview_cache, res->path);
  if (pv) {
    loader_preview_reload(&lfm->loader, pv);
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
  Async *async;
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
  enqueue_and_signal(work->async, (Result *) res);

cleanup:
  free(work);
}


void async_preview_check(Async *async, Preview *pv)
{
  struct preview_check_work *work = malloc(sizeof *work);
  work->async = async;
  work->path = strdup(pv->path);
  work->nrow = pv->nrow;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;
  tpool_add_work(async->tpool, async_preview_check_worker, work);
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
  if (res->version == lfm->loader.preview_cache_version) {
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
  Async *async;
  char *path;
  Preview *preview;
  int width;
  int height;
  uint32_t version;
};


static void async_preview_load_worker(void *arg)
{
  struct preview_load_work *work = arg;

  PreviewLoadResult *res = PreviewLoadResult_create(
      work->preview,
      preview_create_from_file(work->path, work->width, work->height),
      work->version);
  enqueue_and_signal(work->async, (Result *) res);

  free(work->path);
  free(work);
}


void async_preview_load(Async *async, Preview *pv)
{
  struct preview_load_work *work = malloc(sizeof *work);
  work->async = async;
  work->preview = pv;
  work->path = strdup(pv->path);
  work->width = async->lfm->ui.preview.cols;
  work->height = async->lfm->ui.preview.rows;
  work->version = async->lfm->loader.preview_cache_version;
  tpool_add_work(async->tpool, async_preview_load_worker, work);
}

/* }}} */

static inline void *result_create(
  void (*callback)(void *, Lfm *),
  void (*destroy)(void *), size_t size)
{
  struct result_s *res = calloc(1, size);
  res->callback = callback;
  res->destroy = destroy;
  return res;
}

typedef struct chdir_result_s {
  Result super;
  char *path;
} chdir_result;

static void chdir_result_callback(void *p, Lfm *lfm)
{
  chdir_result *res = p;
  if (streq(res->path, lfm->fm.pwd)) {
    if (chdir(res->path) != 0) {
      log_error("chdir: %s: %s", strerror(errno), res->path);
    } else {
      setenv("PWD", res->path, true);
    }
  }
  free(res->path);
  free(res);
}

static void chdir_result_destroy(void *p)
{
  chdir_result *res = p;
  free(res->path);
  free(res);
}

struct chdir_work {
  char *path;
  Async *async;
};

static void async_chdir_worker(void *arg)
{
  struct chdir_work *work = arg;

  struct stat statbuf;

  if (stat(work->path, &statbuf) == -1) {
    free(work->path);
    goto cleanup;
  }

  chdir_result *res = result_create(
      chdir_result_callback,
      chdir_result_destroy,
      sizeof *res);
  res->path = work->path;

  enqueue_and_signal(work->async, (Result *) res);

cleanup:
  free(work);
}

void async_chdir(Async *async, const char *path)
{
  struct chdir_work *work = malloc(sizeof *work);
  work->path = strdup(path);
  work->async = async;
  tpool_add_work(async->tpool, async_chdir_worker, work);
}


typedef struct notify_add_result_s {
  Result super;
  Dir *dir;
  void *version;
  void **version_cmp;
  void *version2;
  void **version2_cmp;
} notify_add_result;

static void notify_add_result_callback(void *p, Lfm *lfm)
{
  notify_add_result *res = p;
  if (res->version == *res->version_cmp && res->version2 == *res->version2_cmp) {
    notify_add_watcher(&lfm->notify, res->dir);
  }
  free(res);
}

static void notify_add_result_destroy(void *p)
{
  notify_add_result *res = p;
  free(res);
}

struct notify_work {
  Async *async;
  char *path;
  Dir *dir;
  void *version;
  void **version_cmp;
  void *version2;
  void **version2_cmp;
};

void async_notify_add_worker(void *arg)
{
  struct notify_work *work = arg;

  struct stat statbuf;

  if (stat(work->path, &statbuf) == -1) {
    goto cleanup;
  }

  notify_add_result *res = result_create(
      notify_add_result_callback,
      notify_add_result_destroy,
      sizeof *res);
  res->dir = work->dir;
  res->version = work->version;
  res->version2 = work->version2;
  res->version_cmp = work->version_cmp;
  res->version2_cmp = work->version2_cmp;

  enqueue_and_signal(work->async, (Result *) res);

cleanup:
  free(work->path);
  free(work);
}

void async_notify_add(Async *async, Dir *dir)
{
  struct notify_work *work = malloc(sizeof *work);
  work->async = async;
  work->path = strdup(dir->path);
  work->dir = dir;
  work->version = (void *) async->lfm->notify.version;
  work->version_cmp = (void **) &async->lfm->notify.version;
  work->version2 = (void *) async->lfm->loader.dir_cache_version;
  work->version2_cmp = (void **) &async->lfm->loader.dir_cache_version;
  tpool_add_work(async->tpool, async_notify_add_worker, work);
}

void async_notify_preview_add(Async *async, Dir *dir)
{
  struct notify_work *work = malloc(sizeof *work);
  work->async = async;
  work->path = strdup(dir->path);
  work->dir = dir;
  work->version = (void *) async->lfm->notify.version;
  work->version_cmp = (void **) &async->lfm->notify.version;
  work->version2 = (void *) async->lfm->fm.dirs.preview;
  work->version2_cmp = (void **) &async->lfm->fm.dirs.preview;
  tpool_add_work(async->tpool, async_notify_add_worker, work);
}
