#include "private.h"

#include "../config.h"
#include "../containers.h"
#include "../dir.h"
#include "../file.h"
#include "../fm.h"
#include "../hooks.h"
#include "../lfm.h"
#include "../loader.h"
#include "../log.h"
#include "../macros.h"
#include "../memory.h"
#include "../stc/cstr.h"
#include "../ui.h"
#include "../util.h"

#include <ev.h>

#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#define FILEINFO_THRESHOLD 200 // send batches of dircounts around every 200ms

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
#include "../stc/vec.h"

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
        dir_cursor_move_to(res->dir, *file_name(file), lfm->fm.height,
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
    work->update = dir_load_flat(zsview_from(work->path), work->level,
                                 work->load_fileinfo);
  } else {
    work->update = dir_load(zsview_from(work->path), work->load_fileinfo);
  }

  uint32_t num_files = vec_file_size(&work->update->files_all);

  if (work->load_fileinfo || num_files == 0) {
    enqueue_and_signal(work->async, (struct result *)work);
    return;
  }

  // prepare list of symlinks/directories to load afterwards
  struct file_path_tup *files = xmalloc(num_files * sizeof *files);
  int j = 0;
  c_foreach(it, vec_file, work->update->files_all) {
    File *file = *it.ref;
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
