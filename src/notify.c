#include <errno.h>
#include <ev.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "loader.h"
#include "log.h"
#include "notify.h"
#include "util.h"

static int inotify_fd = -1;
static int fifo_wd = -1;
static ev_io inotify_watcher;

static struct notify_watcher_data {
  int wd;
  Dir *dir;
} *watchers = NULL;

#define unwatch(t) do { \
    inotify_rm_watch(inotify_fd, (t).wd); \
  } while (0)


static void inotify_cb(EV_P_ ev_io *w, int revents);


int notify_init(App *app)
{
  inotify_fd = inotify_init1(IN_NONBLOCK);

  if (inotify_fd == -1) {
    return -1;
  }

  if ((fifo_wd = inotify_add_watch(inotify_fd, cfg.rundir, IN_CLOSE_WRITE)) == -1) {
    log_error("inotify: %s", strerror(errno));
    return -1;
  }

  ev_io_init(&inotify_watcher, inotify_cb, inotify_fd, EV_READ);
  inotify_watcher.data = app;
  ev_io_start(app->loop, &inotify_watcher);

  return inotify_fd;
}


void notify_deinit()
{
  if (inotify_fd == -1) {
    return;
  }

  cvector_ffree(watchers, unwatch);
  watchers = NULL;
  close(inotify_fd);
  inotify_fd = -1;
}


static inline Dir *get_watcher_data(int wd)
{
  for (size_t i = 0; i < cvector_size(watchers); i++) {
    if (watchers[i].wd == wd) {
      return watchers[i].dir;
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
  App *app = w->data;
  int nread;
  char buf[EVENT_BUFLEN], *p;
  struct inotify_event *event;

  while ((nread = read(inotify_fd, buf, EVENT_BUFLEN)) > 0) {
    for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
      event = (struct inotify_event *) p;

      if (event->len == 0) {
        continue;
      }

      // we use inotify for the fifo because io watchers dont seem to work properly
      // with the fifo, the callback gets called every loop, even with clearerr
      /* TODO: we could filter for our pipe here (on 2021-08-13) */
      if (event->wd == fifo_wd) {
        app_read_fifo(app);
        continue;
      }

      Dir *dir = get_watcher_data(event->wd);
      if (!dir) {
        continue;
      }
      loader_reload(dir);
    }
  }
}


void notify_add_watcher(Dir *dir)
{
  if (inotify_fd == -1) {
    return;
  }

  for (size_t i = 0; i < cvector_size(cfg.inotify_blacklist); i++) {
    if (hasprefix(dir->path, cfg.inotify_blacklist[i])) {
      return;
    }
  }

  for (size_t i = 0; i < cvector_size(watchers); i++) {
    if (watchers[i].dir == dir) {
      return;
    }
  }

  const uint64_t t0 = current_millis();
  int wd = inotify_add_watch(inotify_fd, dir->path, NOTIFY_EVENTS);
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

  cvector_push_back(watchers, ((struct notify_watcher_data) {wd, dir}));
}


void notify_remove_watcher(Dir *dir)
{
  if (inotify_fd == -1) {
    return;
  }

  for (size_t i = 0; i < cvector_size(watchers); i++) {
    if (watchers[i].dir == dir) {
      cvector_swap_ferase(watchers, unwatch, i);
      return;
    }
  }
}


void notify_set_watchers(Dir **dirs, uint16_t n)
{
  if (inotify_fd == -1) {
    return;
  }

  cvector_fclear(watchers, unwatch);

  for (uint16_t i = 0; i < n; i++) {
    if (dirs[i]) {
      notify_add_watcher(dirs[i]);
    }
  }
}
