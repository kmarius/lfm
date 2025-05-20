#include "async.h"

#include "config.h"
#include "dir.h"
#include "dircache.h"
#include "file.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros_defs.h"
#include "memory.h"
#include "notify.h"
#include "path.h"
#include "preview.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

#include <ev.h>

#include <errno.h>
#include <stdint.h>

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <wchar.h>

#define FILEINFO_THRESHOLD 200 // send batches of dircounts around every 200ms

struct result {
  void (*callback)(void *, Lfm *);
  void (*destroy)(void *);
  struct result *next;
};

static struct result *result_queue_get(struct result_queue *queue);

static void async_result_cb(EV_P_ ev_async *w, int revents) {
  (void)revents;
  Async *async = w->data;
  struct result *res;

  pthread_mutex_lock(&async->queue.mutex);
  while ((res = result_queue_get(&async->queue))) {
    res->callback(res, to_lfm(async));
  }
  pthread_mutex_unlock(&async->queue.mutex);

  ev_idle_start(EV_A_ & to_lfm(async)->ui.redraw_watcher);
}

void async_init(Async *async) {
  async->queue.head = NULL;
  async->queue.tail = NULL;
  pthread_mutex_init(&async->queue.mutex, NULL);

  ev_async_init(&async->result_watcher, async_result_cb);
  ev_async_start(to_lfm(async)->loop, &async->result_watcher);

  if (pthread_mutex_init(&async->queue.mutex, NULL) != 0) {
    log_error("pthread_mutex_init: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  async->result_watcher.data = async;

  ev_async_send(EV_DEFAULT_ & async->result_watcher);

  const size_t nthreads = get_nprocs() + 1;
  async->tpool = tpool_create(nthreads);
}

void async_deinit(Async *async) {
  tpool_wait(async->tpool);
  tpool_destroy(async->tpool);

  struct result *res;
  while ((res = result_queue_get(&async->queue))) {
    res->destroy(res);
  }
  pthread_mutex_destroy(&async->queue.mutex);
}

static inline void result_queue_put(struct result_queue *t,
                                    struct result *res) {
  if (!t->head) {
    t->head = res;
    t->tail = res;
  } else {
    t->tail->next = res;
    t->tail = res;
  }
}

static inline struct result *result_queue_get(struct result_queue *t) {
  struct result *res = t->head;

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

static inline void enqueue_and_signal(Async *async, struct result *res) {
  pthread_mutex_lock(&async->queue.mutex);
  result_queue_put(&async->queue, res);
  pthread_mutex_unlock(&async->queue.mutex);
  ev_async_send(EV_DEFAULT_ & async->result_watcher);
}

/* validity checks {{{ */

struct validity_check8 {
  uint8_t *ptr;
  uint8_t val;
};

struct validity_check16 {
  uint16_t *ptr;
  uint16_t val;
};

struct validity_check32 {
  uint32_t *ptr;
  uint32_t val;
};

struct validity_check64 {
  uint64_t *ptr;
  uint64_t val;
};

#define CHECK_INIT(check, value)                                               \
  do {                                                                         \
    assert(sizeof((check).val) == sizeof(value));                              \
    (check).ptr = (typeof((check).ptr))&(value);                               \
    (check).val = (typeof((check).val))(value);                                \
  } while (0)

#define CHECK_PASSES(cmp) (*(cmp).ptr == (cmp).val)

/* }}} */

/* dir_check {{{ */

struct dir_check_data {
  struct result super;
  Async *async;
  char *path;
  Dir *dir; // dir might not exist anymore, don't touch
  time_t loadtime;
  __ino_t ino;
  bool reload;
  struct validity_check64 check; // lfm.loader.dir_cache_version
};

static void dir_check_destroy(void *p) {
  struct dir_check_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void dir_check_callback(void *p, Lfm *lfm) {
  struct dir_check_data *res = p;
  if (CHECK_PASSES(res->check)) {
    if (res->reload) {
      loader_dir_reload(&lfm->loader, res->dir);
    } else {
      res->dir->last_loading_action = 0;
    }
  }
  dir_check_destroy(p);
}

static void async_dir_check_worker(void *arg) {
  struct dir_check_data *work = arg;
  struct stat statbuf;

  if (stat(work->path, &statbuf) == -1 ||
      (statbuf.st_ino == work->ino && statbuf.st_mtime <= work->loadtime)) {
    work->reload = false;
  } else {
    work->reload = true;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_dir_check(Async *async, Dir *dir) {
  struct dir_check_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &dir_check_callback;
  work->super.destroy = &dir_check_destroy;

  if (dir->last_loading_action == 0) {
    dir->last_loading_action = current_millis();
    ui_start_loading_indicator_timer(&to_lfm(async)->ui);
  }

  work->async = async;
  work->path = cstr_strdup(dir_path(dir));
  work->dir = dir;
  work->loadtime = dir->load_time;
  work->ino = dir->stat.st_ino;
  CHECK_INIT(work->check, to_lfm(async)->loader.dir_cache_version);

  log_trace("checking directory %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_dir_check_worker, work, true);
}

/* }}} */

/* fileinfo {{{ */

struct fileinfo {
  File *file;
  int32_t count;    // number of files, if it is a directory, -1 if no result
  struct stat stat; // stat of the link target, if it is a symlink
  int ret;          // -1: stat failed, 0: stat success, >0 no stat result
};

struct file_path_tup {
  File *file;
  char *path;
  __mode_t mode;
};

#define i_TYPE fileinfos, struct fileinfo
#include "stc/vec.h"

struct fileinfo_result {
  struct result super;
  Dir *dir;
  fileinfos infos;
  bool last_batch;
  struct validity_check64 check;
};

static void fileinfo_result_destroy(void *p) {
  struct fileinfo_result *res = p;
  fileinfos_drop(&res->infos);
  xfree(res);
}

static void fileinfo_callback(void *p, Lfm *lfm) {
  struct fileinfo_result *res = p;
  // discard if any other update has been applied in the meantime
  if (CHECK_PASSES(res->check) && !res->dir->has_fileinfo) {
    c_foreach(it, fileinfos, res->infos) {
      if (it.ref->count >= 0) {
        file_dircount_set(it.ref->file, it.ref->count);
      }
      if (it.ref->ret == 0) {
        it.ref->file->stat = it.ref->stat;
      } else if (it.ref->ret == -1) {
        it.ref->file->isbroken = true;
      }
    }
    if (res->last_batch) {
      res->dir->has_fileinfo = true;
    }
    if (res->dir->ind != 0) {
      // if the cursor doesn't rest on the first file, try to reselect it
      File *file = dir_current_file(res->dir);
      dir_sort(res->dir);
      if (file && dir_current_file(res->dir) != file) {
        dir_cursor_move_to(res->dir, file_name(file), lfm->fm.height,
                           cfg.scrolloff);
      }
    } else {
      dir_sort(res->dir);
    }
    fm_update_preview(&lfm->fm);
    ui_redraw(&lfm->ui, REDRAW_FM);
  }
  fileinfo_result_destroy(p);
}

static inline struct fileinfo_result *
fileinfo_result_create(Dir *dir, fileinfos infos, struct validity_check64 check,
                       bool last) {
  struct fileinfo_result *res = xcalloc(1, sizeof *res);
  res->super.callback = &fileinfo_callback;
  res->super.destroy = &fileinfo_result_destroy;

  res->dir = dir;
  res->infos = infos;
  res->last_batch = last;
  res->check = check;
  return res;
}

// Not a worker function because we just call it from async_dir_load_worker
static void async_load_fileinfo(Async *async, Dir *dir,
                                struct validity_check64 check, uint32_t n,
                                struct file_path_tup *files) {
  fileinfos infos = fileinfos_init();

  uint64_t latest = current_millis();

  for (uint32_t i = 0; i < n; i++) {
    if (!S_ISLNK(files[i].mode)) {
      continue;
    }
    struct fileinfo *info =
        fileinfos_push(&infos, ((struct fileinfo){files[i].file, .count = -1}));

    info->ret = stat(files[i].path, &info->stat);
    if (info->ret == 0) {
      files[i].mode = info->stat.st_mode;
    }

    if (current_millis() - latest > FILEINFO_THRESHOLD) {
      struct fileinfo_result *res =
          fileinfo_result_create(dir, infos, check, false);
      enqueue_and_signal(async, (struct result *)res);

      infos = fileinfos_init();
      latest = current_millis();
    }
  }

  for (uint32_t i = 0; i < n; i++) {
    if (!S_ISDIR(files[i].mode)) {
      continue;
    }
    int count = path_dircount(files[i].path);
    fileinfos_push(
        &infos, ((struct fileinfo){files[i].file, .count = count, .ret = 1}));

    if (current_millis() - latest > FILEINFO_THRESHOLD) {
      struct fileinfo_result *res =
          fileinfo_result_create(dir, infos, check, false);
      enqueue_and_signal(async, (struct result *)res);

      infos = fileinfos_init();
      latest = current_millis();
    }
  }

  for (uint32_t i = 0; i < n; i++) {
    xfree(files[i].path);
  }

  struct fileinfo_result *res = fileinfo_result_create(dir, infos, check, true);
  enqueue_and_signal(async, (struct result *)res);

  xfree(files);
}

/* }}} */

/* dir_update {{{ */

struct dir_update_data {
  struct result super;
  Async *async;
  char *path;
  Dir *dir; // dir might not exist anymore, don't touch
  bool load_fileinfo;
  Dir *update;
  uint32_t level;
  struct validity_check64 check; // lfm.loader.dir_cache_version
};

static inline void update_parent_dircount(Lfm *lfm, Dir *dir, uint32_t length) {
  const char *parent_path = path_parent_s(dir_path_str(dir));
  if (parent_path) {
    dircache_value *v = dircache_get_mut(&lfm->loader.dc, parent_path);
    Dir *parent = v ? v->second : NULL;
    if (parent) {
      for (uint32_t i = 0; i < parent->length_all; i++) {
        File *file = parent->files_all[i];
        if (zsview_eq(file_name(file), dir_name(dir))) {
          file_dircount_set(file, length);
          return;
        }
      }
    }
  }
}

static void dir_update_destroy(void *p) {
  struct dir_update_data *res = p;
  dir_destroy(res->update);
  xfree(res->path);
  xfree(res);
}

static void dir_update_callback(void *p, Lfm *lfm) {
  struct dir_update_data *res = p;
  if (CHECK_PASSES(res->check) &&
      res->dir->flatten_level == res->update->flatten_level) {
    if (res->update->length_all != res->dir->length_all) {
      update_parent_dircount(lfm, res->dir, res->update->length_all);
    }
    loader_dir_load_callback(&lfm->loader, res->dir);
    dir_update_with(res->dir, res->update, lfm->fm.height, cfg.scrolloff);
    lfm_run_hook(lfm, LFM_HOOK_DIRUPDATED, dir_path(res->dir));
    if (res->dir->visible) {
      fm_update_preview(&lfm->fm);
      if (fm_current_dir(&lfm->fm) == res->dir) {
        ui_update_file_preview(&lfm->ui);
      }
      ui_redraw(&lfm->ui, REDRAW_FM);
    }
    res->dir->last_loading_action = 0;
    res->update = NULL;
  }
  dir_update_destroy(p);
}

static void async_dir_load_worker(void *arg) {
  struct dir_update_data *work = arg;

  if (work->level > 0) {
    work->update = dir_load_flat(work->path, work->level, work->load_fileinfo);
  } else {
    work->update = dir_load(work->path, work->load_fileinfo);
  }

  const uint32_t num_files = work->update->length_all;

  if (work->load_fileinfo || num_files == 0) {
    enqueue_and_signal(work->async, (struct result *)work);
    return;
  }

  // prepare list of symlinks/directories to load afterwards
  struct file_path_tup *files = xmalloc(num_files * sizeof *files);
  int j = 0;
  for (uint32_t i = 0; i < num_files; i++) {
    File *file = work->update->files_all[i];
    if (S_ISLNK(file->lstat.st_mode) || S_ISDIR(file->lstat.st_mode)) {
      files[j].file = file;
      files[j].path = cstr_strdup(file_path(file));
      files[j].mode = file->lstat.st_mode;
      j++;
    }
  }

  /* Copy these because the main thread can invalidate the work struct in
   * extremely rare cases after we enqueue, and before we call
   * async_load_fileinfo */
  Dir *dir = work->dir;
  Async *async = work->async;
  struct validity_check64 check = work->check;

  enqueue_and_signal(work->async, (struct result *)work);

  async_load_fileinfo(async, dir, check, j, files);
}

void async_dir_load(Async *async, Dir *dir, bool load_fileinfo) {
  struct dir_update_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &dir_update_callback;
  work->super.destroy = &dir_update_destroy;

  dir->has_fileinfo = load_fileinfo;
  dir->status = dir->status == DIR_LOADING_DELAYED ? DIR_LOADING_INITIAL
                                                   : DIR_LOADING_FULLY;
  if (dir->last_loading_action == 0) {
    dir->last_loading_action = current_millis();
    ui_start_loading_indicator_timer(&to_lfm(async)->ui);
  }

  work->async = async;
  work->dir = dir;
  work->path = cstr_strdup(dir_path(dir));
  work->load_fileinfo = load_fileinfo;
  work->level = dir->flatten_level;
  CHECK_INIT(work->check, to_lfm(async)->loader.dir_cache_version);

  log_trace("loading directory %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_dir_load_worker, work, true);
}

/* }}} */

/* preview_check {{{ */

struct preview_check_data {
  struct result super;
  Async *async;
  char *path;
  int height;
  int width;
  time_t mtime;
  uint64_t loadtime;
};

static void preview_check_destroy(void *p) {
  struct preview_check_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void preview_check_callback(void *p, Lfm *lfm) {
  struct preview_check_data *res = p;
  Preview *pv = loader_preview_get(&lfm->loader, res->path);
  if (pv) {
    loader_preview_reload(&lfm->loader, pv);
  }
  preview_check_destroy(p);
}

static void async_preview_check_worker(void *arg) {
  struct preview_check_data *work = arg;
  struct stat statbuf;

  /* TODO: can we actually use st_mtim.tv_nsec? (on 2022-03-07) */
  if (stat(work->path, &statbuf) == -1 ||
      (statbuf.st_mtime <= work->mtime &&
       statbuf.st_mtime <= (long)(work->loadtime / 1000 - 1))) {
    preview_check_destroy(work);
    return;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_preview_check(Async *async, Preview *pv) {
  struct preview_check_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &preview_check_callback;
  work->super.destroy = &preview_check_destroy;
  work->super.next = NULL;

  work->async = async;
  work->path = strdup(pv->path);
  work->height = pv->reload_height;
  work->width = pv->reload_width;
  work->mtime = pv->mtime;
  work->loadtime = pv->loadtime;

  log_trace("checking preview %s", pv->path);
  tpool_add_work(async->tpool, async_preview_check_worker, work, true);
}

/* }}} */

/* preview_load {{{ */

struct preview_load_data {
  struct result super;
  Async *async;
  Preview *preview; // not guaranteed to exist, do not touch
  char *path;
  int width;
  int height;
  Preview *update;
  struct validity_check64 check;
};

static void preview_load_destroy(void *p) {
  struct preview_load_data *res = p;
  preview_destroy(res->update);
  xfree(res->path);
  xfree(res);
}

static void preview_load_callback(void *p, Lfm *lfm) {
  struct preview_load_data *res = p;
  if (CHECK_PASSES(res->check)) {
    preview_update(res->preview, res->update);
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
    res->update = NULL;
  }
  preview_load_destroy(p);
}

static void async_preview_load_worker(void *arg) {
  struct preview_load_data *work = arg;

  work->update =
      preview_create_from_file(work->path, work->width, work->height);
  enqueue_and_signal(work->async, (struct result *)work);
}

void async_preview_load(Async *async, Preview *pv) {
  struct preview_load_data *work = xcalloc(1, sizeof *work);
  work->super.callback = preview_load_callback;
  work->super.destroy = preview_load_destroy;

  pv->status =
      pv->status == PV_LOADING_DELAYED ? PV_LOADING_INITIAL : PV_LOADING_NORMAL;
  pv->loading = true;

  work->async = async;
  work->preview = pv;
  work->path = strdup(pv->path);
  work->width = to_lfm(async)->ui.preview.x;
  work->height = to_lfm(async)->ui.preview.y;
  CHECK_INIT(work->check, to_lfm(async)->loader.preview_cache_version);

  log_trace("loading preview for %s", pv->path);
  tpool_add_work(async->tpool, async_preview_load_worker, work, true);
}

/* }}} */

/* chdir {{{ */

struct chdir_data {
  struct result super;
  Async *async;
  char *path;
  char *origin;
  int err;
  bool run_hook;
};

static void chdir_destroy(void *p) {
  struct chdir_data *res = p;
  xfree(res->path);
  xfree(res->origin);
  xfree(res);
}

static void chdir_callback(void *p, Lfm *lfm) {
  struct chdir_data *res = p;
  if (streq(res->path, lfm->fm.pwd)) {
    lfm_mode_exit(lfm, "visual");
    if (res->err) {
      lfm_error(lfm, "stat: %s", strerror(res->err));
      fm_sync_chdir(&lfm->fm, res->origin, false, false);
    } else if (chdir(res->path) != 0) {
      lfm_error(lfm, "chdir: %s", strerror(errno));
      fm_sync_chdir(&lfm->fm, res->origin, false, false);
    } else {
      setenv("PWD", res->path, true);
      if (res->run_hook) {
        lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST);
      }
    }
  }
  chdir_destroy(p);
}

static void async_chdir_worker(void *arg) {
  struct chdir_data *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    work->err = errno;
  } else {
    work->err = 0;
  }

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_chdir(Async *async, const char *path, bool hook) {
  struct chdir_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &chdir_callback;
  work->super.destroy = &chdir_destroy;

  work->path = strdup(path);
  work->origin = strdup(to_lfm(async)->fm.pwd);
  work->async = async;
  work->run_hook = hook;

  tpool_add_work(async->tpool, async_chdir_worker, work, true);
}

/* }}} */

/* notify {{{ */
struct notify_add_data {
  struct result super;
  Async *async;
  char *path;
  Dir *dir;
  struct validity_check64 check0;
  struct validity_check64 check1;
};

static void notify_add_result_destroy(void *p) {
  struct notify_add_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void notify_add_result_callback(void *p, Lfm *lfm) {
  struct notify_add_data *res = p;
  if (CHECK_PASSES(res->check0) && CHECK_PASSES(res->check1)) {
    notify_add_watcher(&lfm->notify, res->dir);
  }
  notify_add_result_destroy(p);
}

void async_notify_add_worker(void *arg) {
  struct notify_add_data *work = arg;

  struct stat statbuf;
  if (stat(work->path, &statbuf) == -1) {
    notify_add_result_destroy(work);
    return;
  }
  // We open the directory here so that adding the notify watcher
  // can be added immediately. Otherwise, the call to inotify_add_watch
  // can block for several seconds e.g. on automounted nfs mounts.
  DIR *dirp = opendir(work->path);
  if (!dirp) {
    notify_add_result_destroy(work);
    return;
  }
  closedir(dirp);

  enqueue_and_signal(work->async, (struct result *)work);
}

void async_notify_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = cstr_strdup(dir_path(dir));
  work->dir = dir;
  CHECK_INIT(work->check0, to_lfm(async)->notify.version);
  CHECK_INIT(work->check1, to_lfm(async)->loader.dir_cache_version);

  log_trace("watching %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}

void async_notify_preview_add(Async *async, Dir *dir) {
  struct notify_add_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &notify_add_result_callback;
  work->super.destroy = &notify_add_result_destroy;

  work->async = async;
  work->path = cstr_strdup(dir_path(dir));
  work->dir = dir;
  CHECK_INIT(work->check0, to_lfm(async)->notify.version);
  CHECK_INIT(work->check1, to_lfm(async)->fm.dirs.preview);

  log_trace("watching %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_notify_add_worker, work, true);
}

/* }}}*/
