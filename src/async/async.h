#pragma once

#include <ev.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct Dir;
struct Preview;
struct Lfm;
struct bytes;

#include "stc/types.h"
declare_hset(set_ev_child, struct ev_child *);
declare_hset(set_result, struct result *);

struct result_queue {
  struct result *head;
  struct result *tail;
  pthread_mutex_t mutex;
};

typedef struct Async {
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
} Async;

void async_init(Async *async);

void async_deinit(Async *async);

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(Async *async, struct Dir *dir);

// Reloads `dir` from disk.
void async_dir_load(Async *async, struct Dir *dir, bool dircounts);

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(Async *async, struct Preview *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(Async *async, struct Preview *pv);

// change directory to the given path, optionally run on_chdir hook
void async_chdir(Async *async, const char *path, bool hook);

// add an inotify watcher for the given directory
void async_inotify_add(Async *async, struct Dir *dir);

// add an inotify watcher for a previewed directory
void async_inotify_add_previewed(Async *async, struct Dir *dir);

// cancel when dropping dir cache, and before setting multiple watchers
void async_inotify_cancel(Async *async);

// cancel directory checks/loads when clearing the cache
void async_dir_cancel(Async *async);

// kills all preview loading processes, if drop is true, drops the used data
// structures
void async_preview_cancel(Async *async);

// Takes ownership of chunk and arg
void async_lua(struct Async *async, struct bytes chunk, struct bytes arg,
               int ref);

void async_lua_preview(struct Async *async, struct Preview *pv);
