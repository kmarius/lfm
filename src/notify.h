#pragma once

#include "dir.h"
#include "stc/forward.h"

#include <ev.h>

#include <stdbool.h>
#include <stdint.h>

forward_hmap(map_wd_dir, int, Dir *);
forward_hmap(map_dir_wd, Dir *, int);

#define NOTIFY_TIMEOUT 1000 // minimum time between directory reloads
#define NOTIFY_DELAY 50 // delay before reloading after an event is triggered

typedef struct notify {
  ev_io watcher;
  int inotify_fd;
  int fifo_wd;

  map_wd_dir dirs;
  map_dir_wd wds;

  size_t version;
} Notify;

// Returns false on failure
bool notify_init(Notify *notify);

void notify_add_watcher(Notify *notify, Dir *dir);

void notify_remove_watcher(Notify *notify, Dir *dir);

void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n);

static inline void notify_remove_watchers(Notify *notify) {
  notify_set_watchers(notify, NULL, 0);
}

void notify_deinit(Notify *notify);
