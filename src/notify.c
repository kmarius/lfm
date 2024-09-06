#include "notify.h"

#include "config.h"
#include "dir.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros_defs.h"
#include "util.h"

#include <ev.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <unistd.h>

#define i_is_forward
#define i_type map_wd_dir
#define i_key int
#define i_val Dir *
#include "stc/hmap.h"

#define i_is_forward
#define i_type map_dir_wd
#define i_key Dir *
#define i_val int
#include "stc/hmap.h"

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
  c_foreach(it, map_dir_wd, notify->wds) {
    inotify_rm_watch(notify->inotify_fd, it.ref->second);
  }
  map_wd_dir_drop(&notify->dirs);
  map_dir_wd_drop(&notify->wds);
  close(notify->inotify_fd);
  notify->inotify_fd = -1;
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

      map_wd_dir_iter it = map_wd_dir_find(&notify->dirs, event->wd);
      if (it.ref) {
        loader_dir_reload(&lfm->loader, it.ref->second);
      }
    }
  }
}

void notify_add_watcher(Notify *notify, Dir *dir) {
  c_foreach(it, vec_str, cfg.inotify_blacklist) {
    if (hasprefix(dir->path, *it.ref)) {
      return;
    }
  }

  if (map_dir_wd_contains(&notify->wds, dir)) {
    return;
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

  map_wd_dir_insert(&notify->dirs, wd, dir);
  map_dir_wd_insert(&notify->wds, dir, wd);
}

bool notify_remove_watcher(Notify *notify, Dir *dir) {
  map_dir_wd_iter it = map_dir_wd_find(&notify->wds, dir);
  if (it.ref) {
    int wd = it.ref->second;
    inotify_rm_watch(notify->inotify_fd, wd);
    map_dir_wd_erase_at(&notify->wds, it);
    map_wd_dir_erase(&notify->dirs, wd);
    return true;
  }
  return false;
}

void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n) {
  c_foreach(it, map_dir_wd, notify->wds) {
    inotify_rm_watch(notify->inotify_fd, it.ref->second);
  }
  map_dir_wd_clear(&notify->wds);
  map_wd_dir_clear(&notify->dirs);

  for (uint32_t i = 0; i < n; i++) {
    if (dirs[i]) {
      notify_add_watcher(notify, dirs[i]);
    }
  }

  notify->version++;
}
