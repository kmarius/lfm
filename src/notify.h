#pragma once

#include <ev.h>
#include <stdbool.h>
#include <stdint.h>

#include "dir.h"

#define NOTIFY_TIMEOUT 1000 // minimum time between directory reloads
#define NOTIFY_DELAY 50 // delay before reloading after an event is triggered

typedef struct notify_s {
  int inotify_fd;
  int fifo_wd;
  ev_io watcher;

  struct notify_watcher_data {
    int wd;
    Dir *dir;
  } *watchers;

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
