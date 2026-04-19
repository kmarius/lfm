#include "private.h"

#include "defs.h"
#include "dir.h"
#include "file.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <stc/cstr.h>

#include <stdatomic.h>

#define FILEINFO_THRESHOLD 200 // send batches of dircounts around every 200ms

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
#include <stc/vec.h>

struct fileinfo_result {
  struct result super;
  Dir *dir;
  u32 cookie;
  fileinfos infos;
  bool is_last_batch;
};

static void fileinfo_result_destroy(void *p) {
  struct fileinfo_result *res = p;
  fileinfos_drop(&res->infos);
  if (res->is_last_batch)
    dir_dec_ref(res->dir);
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
  map_str_int_emplace(&dir->dircounts, file_name_str(file), tup);
}

static void fileinfo_callback(void *p, Lfm *lfm) {
  struct fileinfo_result *res = p;
  // discard if any other update has been scheduled in the meantime
  if (res->cookie == res->dir->cookie) {
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
        dir_move_cursor_to_name(res->dir, file_name(file));
      }
    } else {
      dir_sort(res->dir, false);
    }
    ui_on_cursor_moved(&lfm->ui, true);
  }
}

static inline struct fileinfo_result *
fileinfo_result_create(Dir *dir, u32 cookie, fileinfos infos, bool is_last) {
  struct fileinfo_result *res = xcalloc(1, sizeof *res);
  res->super.callback = &fileinfo_callback;
  res->super.destroy = &fileinfo_result_destroy;

  res->dir = dir;
  res->cookie = cookie;
  res->infos = infos;
  res->is_last_batch = is_last;
  return res;
}

// Not a worker function because we just call it from async_dir_load_worker
// dircounts will be dropped and not returned to the original directory
// TODO: this creates two fileinfo items for every symlink that points to a
// directory
static void async_load_fileinfo(struct async_ctx *async, Dir *dir, u32 cookie,
                                u32 n, struct file_path_tup *files,
                                map_str_int dircounts) {
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
          fileinfo_result_create(dir, cookie, infos, false);
      submit_async_result(async, (struct result *)res);

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
    map_str_int_iter it = map_str_int_find(&dircounts, files[i].name);
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
          fileinfo_result_create(dir, cookie, infos, false);
      submit_async_result(async, (struct result *)res);

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
      fileinfo_result_create(dir, cookie, infos, true);
  submit_async_result(async, (struct result *)res);

  xfree(files);
  map_str_int_drop(&dircounts);
}

struct dir_update_work {
  struct result super;
  struct async_ctx *async;
  Dir *dir; // only access constant properties, such as path
  u32 cookie;
  bool load_fileinfo;
  Dir *update;
  u32 level;
  map_str_int dircounts;
};

static void dir_update_destroy(void *p) {
  struct dir_update_work *work = p;
  dir_dec_ref(work->dir);
  dir_destroy(work->update);
  map_str_int_drop(&work->dircounts);
  xfree(work);
}

static void dir_update_callback(void *p, Lfm *lfm) {
  struct dir_update_work *work = p;
  Dir *dir = work->dir;
  Dir *update = work->update;
  if (dir->cookie == work->cookie) {
    loader_dir_load_callback(&lfm->loader, dir);
    // apply any keyfuncs before sorting in dir_update_with
    if (dir->settings.sorttype == SORT_LUA)
      lfm_lua_apply_keyfunc(lfm, update, false);
    else if (dir->settings.sorttype == SORT_RAND)
      dir_apply_random_keys(update, dir->settings.salt);
    dir_update_with(dir, update);
    LFM_RUN_HOOK(lfm, LFM_HOOK_DIRUPDATED, dir_path(dir));
    if (dir->visible) {
      if (fm_current_dir(&lfm->fm) == dir)
        ui_on_cursor_moved(&lfm->ui, true);
    }
    dir->last_loading_action = 0;
    work->update = NULL;
  }
}

static void async_dir_load_worker(void *arg) {
  struct dir_update_work *work = arg;
  struct async_ctx *async = work->async;

  map_str_int dircounts = map_str_int_move(&work->dircounts);

  if (work->level == 0) {
    work->update = dir_load(dir_path(work->dir), map_str_int_move(&dircounts),
                            work->load_fileinfo, &async->stop);
  } else {
    if (work->load_fileinfo) {
      // only pass dircounts if we use it now,
      // otherwise it will be passed to the function that loads
      // dircounts, and freed there
      work->update = dir_load_flat(dir_path(work->dir), work->level,
                                   map_str_int_move(&dircounts),
                                   work->load_fileinfo, &async->stop);
    } else {
      work->update =
          dir_load_flat(dir_path(work->dir), work->level, map_str_int_init(),
                        work->load_fileinfo, &async->stop);
    }
  }

  u32 num_files = vec_file_size(&work->update->files_all);

  if (work->load_fileinfo || num_files == 0 ||
      atomic_load_explicit(&async->stop, memory_order_relaxed)) {
    submit_async_result(work->async, (struct result *)work);
    map_str_int_drop(&dircounts);
    if (!work->load_fileinfo)
      dir_dec_ref(work->dir); // release the extra ref

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

  /* Copy these because the main thread can invalidate the work
   * struct in rare cases before we can call async_load_fileinfo */
  Dir *dir = work->dir;

  submit_async_result(work->async, (struct result *)work);

  async_load_fileinfo(async, dir, work->cookie, j, files, dircounts);
}

void async_dir_load(struct async_ctx *async, Dir *dir, bool load_fileinfo) {
  assert(dir->status != DIR_DISOWNED);
  if (dir->status == DIR_DISOWNED) {
    log_error("requested reload of disowned directory: %s", dir_path(dir).str);
    return;
  }
  struct dir_update_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &dir_update_callback;
  work->super.destroy = &dir_update_destroy;

  dir->has_fileinfo = load_fileinfo;
  if (dir->status == DIR_DELAYED)
    dir->status = DIR_SCHEDULED;
  if (dir->last_loading_action == 0) {
    dir->last_loading_action = current_millis();
    ui_start_loading_indicator_timer(&to_lfm(async)->ui);
  }

  // count an extra ref in case we load file info delayed
  // refcounts are decremented when the result struct is destroyed
  // and in case of delayed updates, when the last file info result
  // is destroyed. If the directory contains no files or no additional
  // must be loaded, we remove a reference there.
  dir_inc_ref(dir);
  if (!load_fileinfo)
    dir_inc_ref(dir);

  work->async = async;
  work->dir = dir;
  work->load_fileinfo = load_fileinfo;
  work->level = dir->flatten_level;
  work->dircounts = map_str_int_move(&dir->dircounts);
  // we simply discard the update in the callback if another reload is requested
  // before the previous one is applied.
  work->cookie = ++dir->cookie;

  log_trace("loading directory %s level=%d", dir_path_str(dir),
            dir->flatten_level);
  tpool_add_work(async->tpool, async_dir_load_worker, work, true);
}

void async_dir_cancel(struct async_ctx *async) {
  (void)async;
  c_foreach(it, set_result, async->in_progress.dirs) {
    cancel(*it.ref);
  }
  set_result_clear(&async->in_progress.dirs);
}
