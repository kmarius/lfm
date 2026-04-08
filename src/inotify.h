/*
 * Contains functionality to watch the filesystem via inotify.
 * Directories are added/removed, when changes are detected,
 * a reload is requested (via the directory loader).
 */

#pragma once

#include "defs.h"
#include "dir.h"

#include <ev.h>
#include <stc/types.h>

#include <stdbool.h>

declare_hmap(map_int_dir, int, Dir *);
declare_hmap(map_dir_int, Dir *, int);
declare_hset(set_int, int);

// defaults used in the config, can be changed from lua
#define NOTIFY_TIMEOUT 1000 // minimum time between directory reloads
#define NOTIFY_DELAY 50 // delay before reloading after an event is triggered

struct inotify_ctx {
  int fd;            // fd to inotify instance
  ev_io watcher;     // io watcher for fd
  map_int_dir dirs;  // map currently watched directories to their wds
  map_dir_int wds;   // and vice versa
  set_int wds_dedup; // dedup wds when we receive multiple events
};

// Initialize a Notify context. Returns false on failure.
__lfm_nonnull()
bool inotify_ctx_init(struct inotify_ctx *ctx);

// Deinitialize an inotify context.
__lfm_nonnull()
void inotify_ctx_deinit(struct inotify_ctx *ctx);

// Add a watcher for the directory `dir`.
__lfm_nonnull()
void inotify_add_watcher(struct inotify_ctx *ctx, Dir *dir);

// Remove the watcher for the directory `dir`.
// Returns `true` if the watcher was removed, `false` if it didn't exist.
__lfm_nonnull()
bool inotify_remove_watcher(struct inotify_ctx *ctx, Dir *dir);

// Replace the current set of watchers with `n` watchers for the directories
// passed in `dirs`.
__lfm_nonnull(1)
void inotify_set_watchers(struct inotify_ctx *ctx, Dir **dirs, u32 n);

// Remove all watchers.
static inline void inotify_remove_watchers(struct inotify_ctx *ctx) {
  inotify_set_watchers(ctx, NULL, 0);
}
