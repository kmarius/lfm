/*
 * Contains functionality to watch the filesystem via inotify.
 * Directories are added/removed, when changes are detected,
 * a reload is requested (via the directory loader).
 */

#pragma once

#include "dir.h"
#include "stc/types.h"

#include <ev.h>

#include <stdbool.h>
#include <stdint.h>

declare_hmap(map_wd_dir, int, Dir *);
declare_hmap(map_dir_wd, Dir *, int);

#define NOTIFY_TIMEOUT 1000 // minimum time between directory reloads
#define NOTIFY_DELAY 50 // delay before reloading after an event is triggered

typedef struct notify {
  ev_io watcher;  // io watcher for inotofy_fd
  int inotify_fd; // read from when notified by inotify
  int fifo_wd;    // watch descriptor for the fifo (usually under /run/user/...)
  map_wd_dir dirs; // map currently watched directories to their wds
  map_dir_wd wds;  // and vice versa
  size_t version; // counter that is incremented, every time notify_set_watchers
                  // is called
} Notify;

// Initialize a Notify context. Returns false on failure.
bool notify_init(Notify *notify);

// Add a watcher for the directory `dir`.
void notify_add_watcher(Notify *notify, Dir *dir);

// Remove the watcher for the directory `dir`.
// Returns `true` if the watcher was removed, `false` if it didn't exist.
bool notify_remove_watcher(Notify *notify, Dir *dir);

// Replace the current set of watchers with `n` watchers for the directories
// passed in `dirs`. Incremets the notify->version counter.
void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n);

// Remove all watchers. Incremets the notify->version counter.
static inline void notify_remove_watchers(Notify *notify) {
  notify_set_watchers(notify, NULL, 0);
}

// Deinitialize a Notify context.
void notify_deinit(Notify *notify);
