#include "notify.h"

#include "config.h"
#include "defs.h"
#include "dir.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "loop.h"

#include <ev.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/inotify.h>
#include <unistd.h>

#define i_declared
#define i_type map_int_dir
#define i_key i32
#define i_val Dir *
#include "stc/hmap.h"

#define i_declared
#define i_type map_dir_int
#define i_key Dir *
#define i_val i32
#include "stc/hmap.h"

#define i_declared
#define i_type set_int, int
#include "stc/hset.h"

// should fit at least one inotify event (which might contain a long file name)
#define EVENT_BUFZS 4096

#define NOTIFY_EVENTS                                                          \
  (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

static void inotify_cb(EV_P_ ev_io *w, i32 revents);

bool notify_init(Notify *notify) {
  notify->inotify_fd = inotify_init1(IN_NONBLOCK);
  if (notify->inotify_fd == -1) {
    perror("inotify_init1");
    exit(EXIT_FAILURE);
  }

  ev_io_init(&notify->watcher, inotify_cb, notify->inotify_fd, EV_READ);
  notify->watcher.data = notify;
  ev_io_start(event_loop, &notify->watcher);

  return true;
}

void notify_deinit(Notify *notify) {
  c_foreach(it, map_dir_int, notify->wds) {
    inotify_rm_watch(notify->inotify_fd, it.ref->second);
  }
  map_int_dir_drop(&notify->dirs);
  map_dir_int_drop(&notify->wds);
  set_int_drop(&notify->wds_dedup);
  close(notify->inotify_fd);
  notify->inotify_fd = -1;
}

/* TODO: we currently don't notice if the current directory is deleted while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, i32 revents) {
  (void)loop;
  (void)revents;

  Notify *notify = w->data;

  char buf[EVENT_BUFZS];
  isize nread;
  while ((nread = read(w->fd, buf, sizeof buf)) > 0) {
    struct inotify_event *event;
    for (char *p = buf; p < buf + nread; p += sizeof *event + event->len) {
      event = (struct inotify_event *)p;

      // changes to the directory itself (e.g. from touch'ing it) result
      // in an event without name, we are currently not really interested in
      // those
      // TODO: could we detect deletion?
      if (event->len == 0)
        continue;

      set_int_insert(&notify->wds_dedup, event->wd);
    }
  }
  if (!set_int_is_empty(&notify->wds_dedup)) {
    Loader *loader = &to_lfm(notify)->loader;
    c_foreach(it, set_int, notify->wds_dedup) {
      const map_int_dir_value *v = map_int_dir_get(&notify->dirs, *it.ref);
      if (v)
        loader_dir_reload(loader, v->second);
    }
    set_int_clear(&notify->wds_dedup);
  }
}

void notify_add_watcher(Notify *notify, Dir *dir) {
  c_foreach(it, vec_cstr, cfg.inotify_blacklist) {
    if (zsview_starts_with_sv(dir_path(dir), cstr_sv(it.ref)))
      return;
  }

  if (map_dir_int_contains(&notify->wds, dir))
    return;

  int wd =
      inotify_add_watch(notify->inotify_fd, dir_path_str(dir), NOTIFY_EVENTS);
  if (wd == -1) {
    log_error("inotify: %s", strerror(errno));
    return;
  }

  map_int_dir_insert(&notify->dirs, wd, dir);
  map_dir_int_insert(&notify->wds, dir, wd);
}

bool notify_remove_watcher(Notify *notify, Dir *dir) {
  map_dir_int_iter it = map_dir_int_find(&notify->wds, dir);
  if (!it.ref)
    return false;
  int wd = it.ref->second;
  inotify_rm_watch(notify->inotify_fd, wd);
  map_dir_int_erase_at(&notify->wds, it);
  map_int_dir_erase(&notify->dirs, wd);
  return true;
}

void notify_set_watchers(Notify *notify, Dir **dirs, u32 n) {
  c_foreach(it, map_dir_int, notify->wds) {
    inotify_rm_watch(notify->inotify_fd, it.ref->second);
  }
  map_dir_int_clear(&notify->wds);
  map_int_dir_clear(&notify->dirs);

  for (u32 i = 0; i < n; i++) {
    if (dirs[i]) {
      notify_add_watcher(notify, dirs[i]);
    }
  }
}
