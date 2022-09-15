#include <errno.h>
#include <ev.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "notify.h"
#include "util.h"

static void inotify_cb(EV_P_ ev_io *w, int revents);

bool notify_init(Notify *notify, Lfm *lfm)
{
  notify->inotify_fd = inotify_init1(IN_NONBLOCK);
  if (notify->inotify_fd == -1) {
    return false;
  }

  if ((notify->fifo_wd = inotify_add_watch(notify->inotify_fd, cfg.rundir, IN_CLOSE_WRITE)) == -1) {
    log_error("inotify: %s", strerror(errno));
    return -1;
  }

  ev_io_init(&notify->watcher, inotify_cb, notify->inotify_fd, EV_READ);
  notify->watcher.data = lfm;
  ev_io_start(lfm->loop, &notify->watcher);

  return true;
}

void notify_deinit(Notify *notify)
{
  if (notify->inotify_fd == -1) {
    return;
  }

  cvector_foreach_ptr(struct notify_watcher_data *d, notify->watchers) {
    inotify_rm_watch(notify->inotify_fd, d->wd);
  }
  cvector_free(notify->watchers);
  notify->watchers = NULL;
  close(notify->inotify_fd);
  notify->inotify_fd = -1;
}


static inline Dir *get_watcher_data(Notify *notify, int wd)
{
  for (size_t i = 0; i < cvector_size(notify->watchers); i++) {
    if (notify->watchers[i].wd == wd) {
      return notify->watchers[i].dir;
    }
  }
  return NULL;
}


/* TODO: we currently don't notice if the current directory is deleted while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, int revents)
{
  (void) loop;
  (void) revents;
  Lfm *lfm = w->data;
  Notify *notify = &lfm->notify;
  int nread;
  char buf[EVENT_BUFLEN], *p;
  struct inotify_event *event;

  while ((nread = read(notify->inotify_fd, buf, EVENT_BUFLEN)) > 0) {
    for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
      event = (struct inotify_event *) p;

      if (event->len == 0) {
        continue;
      }

      // we use inotify for the fifo because io watchers dont seem to work properly
      // with the fifo, the callback gets called every loop, even with clearerr
      /* TODO: we could filter for our pipe here (on 2021-08-13) */
      if (event->wd == notify->fifo_wd) {
        lfm_read_fifo(lfm);
        continue;
      }

      Dir *dir = get_watcher_data(notify, event->wd);
      if (!dir) {
        continue;
      }
      loader_dir_reload(&lfm->loader, dir);
    }
  }
}


void notify_add_watcher(Notify *notify, Dir *dir)
{
  if (notify->inotify_fd == -1) {
    return;
  }

  for (size_t i = 0; i < cvector_size(cfg.inotify_blacklist); i++) {
    if (hasprefix(dir->path, cfg.inotify_blacklist[i])) {
      return;
    }
  }

  for (size_t i = 0; i < cvector_size(notify->watchers); i++) {
    if (notify->watchers[i].dir == dir) {
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
   * the only way to work around it is to add notify watches asnc. (on 2021-11-15) */
  if (t1 - t0 > 10) {
    log_warn("inotify_add_watch(fd, \"%s\", ...) took %ums", dir->path, t1 - t0);
  }

  cvector_push_back(notify->watchers, ((struct notify_watcher_data) {wd, dir}));
}


void notify_remove_watcher(Notify *notify, Dir *dir)
{
  if (notify->inotify_fd == -1) {
    return;
  }

  for (size_t i = 0; i < cvector_size(notify->watchers); i++) {
    if (notify->watchers[i].dir == dir) {
      inotify_rm_watch(notify->inotify_fd, notify->watchers[i].wd);
      cvector_swap_erase(notify->watchers, i);
      return;
    }
  }
}


void notify_set_watchers(Notify *notify, Dir **dirs, uint32_t n)
{
  if (notify->inotify_fd == -1) {
    return;
  }

  cvector_foreach_ptr(struct notify_watcher_data *d, notify->watchers) {
    inotify_rm_watch(notify->inotify_fd, d->wd);
  }

  for (uint32_t i = 0; i < n; i++) {
    if (dirs[i]) {
      notify_add_watcher(notify, dirs[i]);
    }
  }
}
