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
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "notify.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

#define DIRCOUNT_THRESHOLD 200  // send batches of dircounts around every 200ms

struct result_s {
  void (*callback)(void *, Lfm *);
  void (*destroy)(void *);
  struct result_s *next;
};

static struct result_s *result_queue_get(struct result_queue *queue);

static void async_result_cb(EV_P_ ev_async *w, int revents)
{
  (void) revents;
  Async *async = w->data;
  struct result_s *res;

  pthread_mutex_lock(&async->queue.mutex);
  while ((res = result_queue_get(&async->queue))) {
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

  struct result_s *res;
  while ((res = result_queue_get(&async->queue))) {
    res->destroy(res);
  }
  pthread_mutex_destroy(&async->queue.mutex);
}

static inline void result_queue_put(struct result_queue *t, struct result_s *res)
{
  if (!t->head) {
    t->head = res;
    t->tail = res;
  } else {
    t->tail->next = res;
    t->tail = res;
  }
}

static inline struct result_s *result_queue_get(struct result_queue *t)
{
  struct result_s *res = t->head;

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

static inline void enqueue_and_signal(Async *async, struct result_s *res)
{
  pthread_mutex_lock(&async->queue.mutex);
  result_queue_put(&async->queue, res);
  pthread_mutex_unlock(&async->queue.mutex);
  ev_async_send(EV_DEFAULT_ &async->result_watcher);
}

/* dir_check {{{ */

struct dir_check_s {
  struct result_s super;
  Async *async;
  char *path;
  Dir *dir;         // dir might not exist anymore, don't touch
  uint32_t version;
  time_t loadtime;
};

static void dir_check_callback(void *p, Lfm *lfm)
{
  struct dir_check_s *res = p;
  if (res->version == lfm->loader.dir_cache_version) {
    loader_dir_reload(&lfm->loader, res->dir);
  }
  free(res);
}

static void dir_check_destroy(void *p)
{
  struct dir_check_s *res = p;
  free(res);
}

static void async_dir_check_worker(void *arg)
{
  struct dir_check_s *work = arg;
  struct stat statbuf;

  if (stat(work->dir->path, &statbuf) == -1
      || statbuf.st_mtime <= work->loadtime) {
    free(work->path);
    free(work);
    return;
  }

  free(work->path);
  work->path = NULL;

  enqueue_and_signal(work->async, (struct result_s *) work);
}

void async_dir_check(Async *async, Dir *dir)
{
  struct dir_check_s *work = calloc(1, sizeof *work);
  work->super.callback = &dir_check_callback;
  work->super.destroy = &dir_check_destroy;

  work->async = async;
  work->path = strdup(dir->path);
  work->dir = dir;
  work->loadtime = dir->load_time;
  work->version = async->lfm->loader.dir_cache_version;
  tpool_add_work(async->tpool, async_dir_check_worker, work);
}

/* }}} */

/* dir_count {{{ */

struct dir_count_s {
  struct result_s super;
  Dir *dir;
  struct dircount {
    File *file;
    uint32_t count;
  } *dircounts;
  bool last;
  uint32_t version;
};

struct file_path_tup {
  File *file;
  char *path;
};

static void dir_count_callback(void *p, Lfm *lfm)
{
  struct dir_count_s *res = p;
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

static void dir_count_destroy(void *p)
{
  struct dir_count_s *res = p;
  cvector_free(res->dircounts);
  free(res);
}

static inline struct dir_count_s *dir_count_create(Dir *dir, struct dircount* files, uint32_t version, bool last)
{
  struct dir_count_s *res = calloc(1, sizeof *res);
  res->super.callback = &dir_count_callback;
  res->super.destroy = &dir_count_destroy;

  res->dir = dir;
  res->dircounts = files;
  res->last = last;
  res->version = version;
  return res;
}

// Not a worker function because we just call it from async_dir_load_worker
static void async_load_dircounts(Async *async, Dir *dir, uint32_t version, uint32_t n, struct file_path_tup *files)
{
  cvector_vector_type(struct dircount) counts = NULL;

  uint64_t latest = current_millis();

  for (uint32_t i = 0; i < n; i++) {
    cvector_push_back(counts, ((struct dircount) {files[i].file, path_dircount(files[i].path)}));
    free(files[i].path);

    if (current_millis() - latest > DIRCOUNT_THRESHOLD) {
      struct dir_count_s *res = dir_count_create(dir, counts, version, false);
      enqueue_and_signal(async, (struct result_s *) res);

      counts = NULL;
      latest = current_millis();
    }
  }

  struct dir_count_s *res = dir_count_create(dir, counts, version, true);
  enqueue_and_signal(async, (struct result_s *) res);

  free(files);
}

/* }}} */

/* dir_update {{{ */

struct dir_update_s {
  struct result_s super;
  Async *async;
  char *path;
  Dir *dir;         // dir might not exist anymore, don't touch
  bool dircounts;
  Dir *update;
  uint32_t version;
  uint32_t level;
};

static void dir_update_callback(void *p, Lfm *lfm)
{
  struct dir_update_s *res = p;
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
    lfm_run_hook1(lfm, LFM_HOOK_DIRUPDATED, res->dir->path);
    if (res->dir->visible) {
      fm_update_preview(&lfm->fm);
      ui_redraw(&lfm->ui, REDRAW_FM);
    }
  } else {
    dir_destroy(res->update);
  }
  free(res);
}

static void dir_update_destroy(void *p)
{
  struct dir_update_s *res = p;
  dir_destroy(res->update);
  free(res);
}

static void async_dir_load_worker(void *arg)
{
  struct dir_update_s *work = arg;

  if (work->level > 0) {
    work->update = dir_load_flat(work->path, work->level, work->dircounts);
  } else {
    work->update = dir_load(work->path, work->dircounts);
  }

  const uint32_t num_files = work->update->length_all;
  struct file_path_tup *files = NULL;
  if (!work->dircounts && num_files > 0) {
    files = malloc(num_files * sizeof *files);
    for (uint32_t i = 0; i < num_files; i++) {
      files[i].file = work->update->files_all[i];
      files[i].path = strdup(work->update->files_all[i]->path);
    }
  }

  free(work->path);
  work->path = NULL;

  enqueue_and_signal(work->async, (struct result_s *) work);

  if (!work->dircounts && num_files > 0) {
    async_load_dircounts(work->async, work->dir, work->version, num_files, files);
  }
}

void async_dir_load(Async *async, Dir *dir, bool dircounts)
{
  struct dir_update_s *work = calloc(1, sizeof *work);
  work->super.callback = &dir_update_callback;
  work->super.destroy = &dir_update_destroy;

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

struct preview_check_s {
  struct result_s super;
  Async *async;
  char *path;
  int nrow;
  time_t mtime;
  uint64_t loadtime;
};

static void preview_check_callback(void *p, Lfm *lfm)
{
  struct preview_check_s *res = p;
  Preview *pv = ht_get(lfm->loader.preview_cache, res->path);
  if (pv) {
    loader_preview_reload(&lfm->loader, pv);
  }
  free(res->path);
  free(res);
}

static void preview_check_destroy(void *p)
{
  struct preview_check_s *res = p;
  free(res->path);
  free(res);
}

static void async_preview_check_worker(void *arg)
{
  struct preview_check_s *work = arg;
  struct stat statbuf;

  /* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
  if (stat(work->path, &statbuf) == -1
      || (statbuf.st_mtime <= work->mtime
        && statbuf.st_mtime <= (long) (work->loadtime / 1000 - 1))) {
    free(work->path);
    free(work);
    return;
  }

  enqueue_and_signal(work->async, (struct result_s *) work);
}

void async_preview_check(Async *async, Preview *pv)
{
  struct preview_check_s *work = calloc(1, sizeof *work);
  work->super.callback = &preview_check_callback;
  work->super.destroy = &preview_check_destroy;
  work->super.next = NULL;

  work->async = async;
  work->path = strdup(pv->path);
  work->nrow = pv->nrow;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;
  tpool_add_work(async->tpool, async_preview_check_worker, work);
}

/* }}} */

/* preview_load {{{ */

struct preview_load_s {
  struct result_s super;
  Async *async;
  Preview *preview;  // not guaranteed to exist, do not touch
  char *path;
  int width;
  int height;
  Preview *update;
  uint32_t version;
};

static void preview_load_callback(void *p, Lfm *lfm)
{
  struct preview_load_s *res = p;
  if (res->version == lfm->loader.preview_cache_version) {
    preview_update(res->preview, res->update);
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
  } else {
    preview_destroy(res->update);
  }
  free(res);
}

static void preview_load_destroy(void *p)
{
  struct preview_load_s *res = p;
  preview_destroy(res->update);
  free(res);
}

static void async_preview_load_worker(void *arg)
{
  struct preview_load_s *work = arg;

  work->update = preview_create_from_file(work->path, work->width, work->height);
  enqueue_and_signal(work->async, (struct result_s *) work);

  free(work->path);
  work->path = NULL;
}

void async_preview_load(Async *async, Preview *pv)
{
  struct preview_load_s *work = calloc(1, sizeof *work);
  work->super.callback = preview_load_callback;
  work->super.destroy = preview_load_destroy;

  work->async = async;
  work->preview = pv;
  work->path = strdup(pv->path);
  work->width = async->lfm->ui.preview.cols;
  work->height = async->lfm->ui.preview.rows;
  work->version = async->lfm->loader.preview_cache_version;
  tpool_add_work(async->tpool, async_preview_load_worker, work);
}

/* }}} */

/* chdir {{{ */

struct chdir_s {
  struct result_s super;
  Async *async;
  char *path;
  bool hook;
};

static void chdir_callback(void *p, Lfm *lfm)
{
  struct chdir_s *res = p;
  if (streq(res->path, lfm->fm.pwd)) {
    if (chdir(res->path) != 0) {
      log_error("chdir: %s: %s", strerror(errno), res->path);
    } else {
      setenv("PWD", res->path, true);
      if (res->hook) {
        lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST);
      }
    }
  }
  free(res->path);
  free(res);
}

static void chdir_destroy(void *p)
{
  struct chdir_s *res = p;
  free(res->path);
  free(res);
}

static void async_chdir_worker(void *arg)
{
  struct chdir_s *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    free(work->path);
    free(work);
    return;
  }

  enqueue_and_signal(work->async, (struct result_s *) work);
}

void async_chdir(Async *async, const char *path, bool hook)
{
  struct chdir_s *work = calloc(1, sizeof *work);
  work->super.callback = &chdir_callback,
    work->super.destroy = &chdir_destroy,

    work->path = strdup(path);
  work->async = async;
  work->hook = hook;
  tpool_add_work(async->tpool, async_chdir_worker, work);
}

/* }}} */

/* notify {{{ */

struct notify_add_s {
  struct result_s super;
  Async *async;
  char *path;
  Dir *dir;
  void *version;
  void **version_cmp;
  void *version2;
  void **version2_cmp;
};

static void notify_add_result_callback(void *p, Lfm *lfm)
{
  struct notify_add_s *res = p;
  if (res->version == *res->version_cmp && res->version2 == *res->version2_cmp) {
    notify_add_watcher(&lfm->notify, res->dir);
  }
  free(res);
}

static void notify_add_result_destroy(void *p)
{
  struct notify_add_s *res = p;
  free(res);
}

void async_notify_add_worker(void *arg)
{
  struct notify_add_s *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    free(work->path);
    free(work);
    return;
  }

  free(work->path);
  work->path = NULL;

  enqueue_and_signal(work->async, (struct result_s *) work);
}

void async_notify_add(Async *async, Dir *dir)
{
  struct notify_add_s *work = calloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

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
  struct notify_add_s *work = calloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = strdup(dir->path);
  work->dir = dir;
  work->version = (void *) async->lfm->notify.version;
  work->version_cmp = (void **) &async->lfm->notify.version;
  work->version2 = (void *) async->lfm->fm.dirs.preview;
  work->version2_cmp = (void **) &async->lfm->fm.dirs.preview;
  tpool_add_work(async->tpool, async_notify_add_worker, work);
}

/* }}}*/
