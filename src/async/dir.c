#include "private.h"

#include "config.h"
#include "defs.h"
#include "dir.h"
#include "file.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "stc/cstr.h"
#include "ui.h"
#include "util.h"

#include <ev.h>

#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
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
  struct validity_check64 check0; // lfm.loader.dir_cache_version
};

static void dir_check_destroy(void *p) {
  struct dir_check_data *res = p;
  xfree(res->path);
  xfree(res);
}

static void dir_check_callback(void *p, Lfm *lfm) {
  struct dir_check_data *res = p;
  if (CHECK_PASSES(res->check0)) {
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
  work->path = zsview_strdup(dir_path(dir));
  work->dir = dir;
  work->loadtime = dir->load_time;
  work->ino = dir->stat.st_ino;
  CHECK_INIT(work->check0, to_lfm(async)->loader.dir_cache_version);

  log_trace("checking directory %s", dir_path_str(dir));
  tpool_add_work(async->tpool, async_dir_check_worker, work, true);
}

struct fileinfo {
  File *file;
  i32 count;        // number of files, if it is a directory, -1 if no result
  struct stat stat; // stat of the link target, if it is a symlink
  int ret;          // -1: stat failed, 0: stat success, >0 no stat result
};

struct file_path_tup {
  File *file; // target file, must not be used, used to apply the result
  char *path;
  const char *name;
  __mode_t mode;
  time_t mtime;
};

#define i_TYPE fileinfos, struct fileinfo
#include "../stc/vec.h"

struct fileinfo_result {
  struct result super;
  Dir *dir;
  fileinfos infos;
  bool is_last_batch;
  struct validity_check64 check0;
  struct validity_check32 check1;
};

static void fileinfo_result_destroy(void *p) {
  struct fileinfo_result *res = p;
  fileinfos_drop(&res->infos);
  xfree(res);
}

// set the dir count for a file in the given directory,
// updates the dircounts cache in the directory
static inline void set_dircount(Dir *dir, File *file, u32 count) {
  file_set_dircount(file, count);
  struct tuple_mtime_count tup = {
      .count = count,
      .mtime = file->stat.st_mtim.tv_sec,
  };
  hmap_dircount_emplace(&dir->dircounts, file_name_str(file), tup);
}

static void fileinfo_callback(void *p, Lfm *lfm) {
  struct fileinfo_result *res = p;
  // discard if any other update has been applied in the meantime
  if (CHECK_PASSES(res->check0) && CHECK_PASSES(res->check1) &&
      !res->dir->has_fileinfo) {
    c_foreach(it, fileinfos, res->infos) {
      if (it.ref->ret == 0) {
        it.ref->file->stat = it.ref->stat;
      } else if (it.ref->ret == -1) {
        it.ref->file->isbroken = true;
      }

      if (it.ref->count >= 0) {
        set_dircount(res->dir, it.ref->file, it.ref->count);
      }
    }
    if (res->is_last_batch) {
      res->dir->has_fileinfo = true;
    }
    if (res->dir->ind != 0) {
      // if the cursor doesn't rest on the first file, try to reselect it
      File *file = dir_current_file(res->dir);
      dir_sort(res->dir, false);
      if (file && dir_current_file(res->dir) != file) {
        dir_move_cursor_to_name(res->dir, file_name(file), lfm->fm.height,
                                cfg.scrolloff);
      }
    } else {
      dir_sort(res->dir, false);
    }
    fm_update_preview(&lfm->fm, true);
    ui_redraw(&lfm->ui, REDRAW_FM);
  }
  fileinfo_result_destroy(p);
}

static inline struct fileinfo_result *
fileinfo_result_create(Dir *dir, fileinfos infos,
                       struct validity_check64 check0,
                       struct validity_check32 check1, bool is_last) {
  struct fileinfo_result *res = xcalloc(1, sizeof *res);
  res->super.callback = &fileinfo_callback;
  res->super.destroy = &fileinfo_result_destroy;

  res->dir = dir;
  res->infos = infos;
  res->is_last_batch = is_last;
  res->check0 = check0;
  res->check1 = check1;
  return res;
}

// Not a worker function because we just call it from async_dir_load_worker
// dircounts will be dropped and not returned to the original directory
// TODO: this creates two fileinfo items for every symlink that points to a
// directory
static void async_load_fileinfo(Async *async, Dir *dir,
                                struct validity_check64 check0,
                                struct validity_check32 check1, u32 n,
                                struct file_path_tup *files,
                                hmap_dircount dircounts) {
  fileinfos infos = fileinfos_init();

  u64 latest = current_millis();

  // stat for symbolic links
  for (u32 i = 0; i < n; i++) {
    if (!S_ISLNK(files[i].mode)) {
      continue;
    }
    struct fileinfo *info =
        fileinfos_push(&infos, ((struct fileinfo){files[i].file, .count = -1}));

    info->ret = stat(files[i].path, &info->stat);
    if (info->ret == 0) {
      // make sure we load directory counts afterwards
      files[i].mode = info->stat.st_mode;
    }

    u64 now = current_millis();
    if (now - latest > FILEINFO_THRESHOLD) {
      struct fileinfo_result *res =
          fileinfo_result_create(dir, infos, check0, check1, false);
      enqueue_and_signal(async, (struct result *)res);

      infos = fileinfos_init();
      latest = now;
      if (atomic_load_explicit(&async->stop, memory_order_relaxed))
        goto finalize;
    }
  }

  // load dircounts for directories
  for (u32 i = 0; i < n; i++) {
    if (!S_ISDIR(files[i].mode)) {
      continue;
    }

    struct fileinfo *info =
        fileinfos_push(&infos, ((struct fileinfo){files[i].file, .ret = 1}));

    // try to get dircounts from cache
    int count = -1;
    hmap_dircount_iter it = hmap_dircount_find(&dircounts, files[i].name);
    if (it.ref) {
      struct tuple_mtime_count tup = it.ref->second;
      if (tup.mtime == files[i].mtime) {
        // use cached data
        count = tup.count;
      }
    }

    if (count < 0)
      count = path_dircount(files[i].path);
    info->count = count;

    u64 now = current_millis();
    if (now - latest > FILEINFO_THRESHOLD) {
      struct fileinfo_result *res =
          fileinfo_result_create(dir, infos, check0, check1, false);
      enqueue_and_signal(async, (struct result *)res);

      infos = fileinfos_init();
      latest = now;

      if (atomic_load_explicit(&async->stop, memory_order_relaxed))
        goto finalize;
    }
  }

finalize:
  for (u32 i = 0; i < n; i++) {
    xfree(files[i].path);
  }

  struct fileinfo_result *res =
      fileinfo_result_create(dir, infos, check0, check1, true);
  enqueue_and_signal(async, (struct result *)res);

  xfree(files);
  hmap_dircount_drop(&dircounts);
}

struct dir_update_data {
  struct result super;
  Async *async;
  char *path;
  Dir *dir; // dir might not exist anymore, don't touch
  bool load_fileinfo;
  Dir *update;
  u32 level;
  hmap_dircount dircounts;
  struct validity_check64 check0; // lfm.loader.dir_cache_version
  struct validity_check32 check1; // dir.version
};

static void dir_update_destroy(void *p) {
  struct dir_update_data *res = p;
  dir_destroy(res->update);
  hmap_dircount_drop(&res->dircounts);
  xfree(res->path);
  xfree(res);
}

static void dir_update_callback(void *p, Lfm *lfm) {
  struct dir_update_data *res = p;
  if (CHECK_PASSES(res->check0) &&
      res->dir->flatten_level == res->update->flatten_level) {
    loader_dir_load_callback(&lfm->loader, res->dir);
    dir_update_with(res->dir, res->update, lfm->fm.height, cfg.scrolloff);
    LFM_RUN_HOOK(lfm, LFM_HOOK_DIRUPDATED, dir_path(res->dir));
    if (res->dir->visible) {
      fm_update_preview(&lfm->fm, true);
      if (fm_current_dir(&lfm->fm) == res->dir) {
        ui_update_preview(&lfm->ui, true);
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
  Async *async = work->async;

  hmap_dircount dircounts = hmap_dircount_move(&work->dircounts);

  if (work->level == 0) {
    work->update =
        dir_load(zsview_from(work->path), hmap_dircount_move(&dircounts),
                 work->load_fileinfo, &async->stop);
  } else {
    if (work->load_fileinfo) {
      // only pass dircounts if we use it now,
      // otherwise it will be passed to the function that loads
      // dircounts, and freed there
      work->update = dir_load_flat(zsview_from(work->path), work->level,
                                   hmap_dircount_move(&dircounts),
                                   work->load_fileinfo, &async->stop);
    } else {
      work->update = dir_load_flat(zsview_from(work->path), work->level,
                                   hmap_dircount_init(), work->load_fileinfo,
                                   &async->stop);
    }
  }

  u32 num_files = vec_file_size(&work->update->files_all);

  if (work->load_fileinfo || num_files == 0 ||
      atomic_load_explicit(&async->stop, memory_order_relaxed)) {
    enqueue_and_signal(work->async, (struct result *)work);
    hmap_dircount_drop(&dircounts);
    return;
  }

  // prepare list of symlinks/directories to load afterwards
  struct file_path_tup *files = xmalloc(num_files * sizeof *files);
  int j = 0;
  c_foreach(it, vec_file, work->update->files_all) {
    File *file = *it.ref;
    if (S_ISLNK(file->lstat.st_mode) || S_ISDIR(file->lstat.st_mode)) {
      files[j].file = file;
      files[j].path = zsview_strdup(file_path(file));
      // if the directory is flattened, this can contain leading path components
      files[j].name =
          files[j].path + (file_name_str(file) - file_path_str(file));
      files[j].mode = file->lstat.st_mode;
      files[j].mtime = file->stat.st_mtim.tv_sec;
      j++;
    }
  }

  /* Copy these because the main thread can invalidate the work struct in
   * extremely rare cases after we enqueue, and before we call
   * async_load_fileinfo */
  Dir *dir = work->dir;
  struct validity_check64 check0 = work->check0;
  struct validity_check32 check1 = work->check1;

  enqueue_and_signal(work->async, (struct result *)work);

  async_load_fileinfo(async, dir, check0, check1, j, files, dircounts);
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
  work->path = zsview_strdup(dir_path(dir));
  work->load_fileinfo = load_fileinfo;
  work->level = dir->flatten_level;
  work->dircounts = hmap_dircount_move(&dir->dircounts);

  dir->version++;
  CHECK_INIT(work->check0, to_lfm(async)->loader.dir_cache_version);
  CHECK_INIT(work->check1, dir->version);

  log_trace("loading directory %s level=%d", dir_path_str(dir),
            dir->flatten_level);
  tpool_add_work(async->tpool, async_dir_load_worker, work, true);
}
