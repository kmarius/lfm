#include "notify.h"

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros.h"
#include "util.h"

#include <ev.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <unistd.h>

// This is plenty of space, most file names are shorter and as long as
// *one* event fits we should not get overwhelmed
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128 // max filename length, arbitrary
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

#define NOTIFY_EVENTS                                                          \
  (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

static void inotify_cb(EV_P_ ev_io *w, int revents);

bool notify_init(Notify *notify) {
  notify->inotify_fd = inotify_init1(IN_NONBLOCK);
  if (notify->inotify_fd == -1) {
    fprintf(stderr, "inotify_init1: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ev_io_init(&notify->watcher, inotify_cb, notify->inotify_fd, EV_READ);
  Lfm *lfm = to_lfm(notify);
  notify->watcher.data = lfm;
  ev_io_start(lfm->loop, &notify->watcher);

  return true;
}

void notify_deinit(Notify *notify) {
  cvector_foreach_ptr(struct notify_watcher_data * d, notify->watchers) {
    inotify_rm_watch(notify->inotify_fd, d->wd);
  }
  cvector_free(notify->watchers);
  notify->watchers = NULL;
  close(notify->inotify_fd);
  notify->inotify_fd = -1;
}

static inline Dir *get_watched_dir(Notify *notify, int wd) {
  cvector_foreach_ptr(struct notify_watcher_data * d, notify->watchers) {
    if (d->wd == wd) {
      return d->dir;
    }
  }
  return NULL;
}

/* TODO: we currently don't notice if the current directory is deleted while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, int revents) {
  (void)loop;
  (void)revents;

  Lfm *lfm = w->data;
  Notify *notify = &lfm->notify;

  int nread;
  char buf[EVENT_BUFLEN], *p;
  struct inotify_event *event;

  while ((nread = read(notify->inotify_fd, buf, EVENT_BUFLEN)) > 0) {
    for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
      event = (struct inotify_event *)p;

      if (event->len == 0) {
        continue;
      }

      Dir *dir = get_watched_dir(notify, event->wd);
      if (dir) {
        loader_dir_reload(&lfm->loader, dir);
      }
    }
  }
}

void notify_add_watcher(Notify *notify, Dir *dir) {
  cvector_foreach(const char *s, cfg.inotify_blacklist) {
    if (hasprefix(dir->path, s)) {
      return;
    }
  }

  cvector_foreach_ptr(struct notify_watcher_data * d, notify->watchers) {
    if (d->dir == dir) {
      return;
    }
  }

  const uint64_t t0 = current_millis();
  int wd = inotify_add_watch(notify->inotify_fd, dir->path, NOTIFY_EVENTS);
  if (wd == -1) {
    log_error("inotify: %s", strerror(errno));
    return;
  }
  const uint64_t t1 = current_millis();

  /* TODO: inotify_add_watch can take over 200ms for example on samba shares.
   * the only way to work around it is to add notify watches asnc. (on
   * 2021-11-15) */
  if (t1 - t0 > 10) {
    log_warn("inotify_add_watch(fd, \"%s\", ...) took %ums", dir->path,
             t1 - t0);
  }

  cvector_push_back(notify->watchers, ((struct notify_watcher_data){wd, dir}));
}

void notify_remove_watcher(Notify *notify, Dir *dir) {
  cvector_foreach_ptr(struct notify_watcher_data * data, notify->watchers) {
    if (data->dir == dir) {
      inotify_rm_watch(notify->inotify_fd, data->wd);
      cvector_swap_erase(notify->watchers, (size_t)(data - notify->watchers));
      return;
    }
  }
}

void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n) {
  cvector_foreach_ptr(struct notify_watcher_data * d, notify->watchers) {
    inotify_rm_watch(notify->inotify_fd, d->wd);
  }
  cvector_set_size(notify->watchers, 0);

  for (uint32_t i = 0; i < n; i++) {
    if (dirs[i]) {
      notify_add_watcher(notify, dirs[i]);
    }
  }

  notify->version++;
}
