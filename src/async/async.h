#pragma once

#include "types/bytes.h"
#include "types/vec_bytes.h"

#include <ev.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

struct Dir;
struct Preview;
struct Lfm;

#include <stc/types.h>
declare_hset(set_ev_child, struct ev_child *);
declare_hset(set_result, struct result *);

struct result_queue {
  struct result *head;
  struct result *tail;
  pthread_mutex_t mutex;
};

struct async_ctx {
  struct tpool *tpool;
  struct result_queue queue;
  atomic_bool stop; // we store/load with relaxed
  ev_async result_watcher;
  struct { // Data we track for cancelling and quicker shutdown.
    // we add the respective work items when adding them to the work queue
    // and remove them in the callback. If we cancel work, the callback is
    // not called, so it must be removed if needed.
    set_ev_child previewer_children;
    set_result lua_previews;
    set_result dirs;
    set_result inotify;
    struct result *inotify_preview;
    struct result *chdir;
  } in_progress;
};

void async_ctx_init(struct async_ctx *async);

void async_ctx_deinit(struct async_ctx *async);

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(struct async_ctx *async, struct Dir *dir);

// Reloads `dir` from disk.
void async_dir_load(struct async_ctx *async, struct Dir *dir, bool dircounts);

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(struct async_ctx *async, struct Preview *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(struct async_ctx *async, struct Preview *pv);

// change directory to the given path, optionally run on_chdir hook
void async_chdir(struct async_ctx *async, const char *path, bool run_hook);

// add an inotify watcher for the given directory
void async_inotify_add(struct async_ctx *async, struct Dir *dir);

// add an inotify watcher for a previewed directory
void async_inotify_add_previewed(struct async_ctx *async, struct Dir *dir);

// cancel when dropping dir cache, and before setting multiple watchers
void async_inotify_cancel(struct async_ctx *async);

// cancel directory checks/loads when clearing the cache
void async_dir_cancel(struct async_ctx *async);

// kills all preview loading processes, if drop is true, drops the used data
// structures
void async_preview_cancel(struct async_ctx *async);

// Takes ownership of chunk and arg
void async_lua(struct async_ctx *async, struct bytes chunk,
               struct vec_bytes args, int ref);

void async_lua_preview(struct async_ctx *async, struct Preview *pv);
