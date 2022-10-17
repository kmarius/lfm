#pragma once

#include <ev.h>
#include <stdbool.h>
#include <stdint.h>

#include "dir.h"

#define NOTIFY_TIMEOUT 1000  // minimum time between directory reloads
#define NOTIFY_DELAY 50  // delay before reloading after an event is triggered

#define NOTIFY_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

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

// This is plenty of space, most file names are shorter and as long as
// *one* event fits we should not get overwhelmed
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  // max filename length, arbitrary
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

struct lfm_s;
// Returns false on failure
bool notify_init(Notify *notify, struct lfm_s *lfm);

void notify_add_watcher(Notify *notify, Dir *dir);

void notify_remove_watcher(Notify *notify, Dir *dir);

void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n);

void notify_deinit(Notify *notify);
