#include "fifo.h"

#include "config.h"
#include "lfm.h"
#include "log.h"

#include <errno.h>
#include <ev.h>
#include <fcntl.h>

static ev_io fifo_watcher;
static int fifo_fd = -1;

static void fifo_cb(EV_P_ ev_io *w, int revents);

int fifo_init(Lfm *lfm) {
  log_trace("setting up fifo");

  if ((mkfifo(cstr_str(&cfg.fifopath), 0600) == -1 && errno != EEXIST)) {
    log_error("mkfifo: %s", strerror(errno));
    return -1;
  }

  fifo_fd = open(cstr_str(&cfg.fifopath), O_RDWR | O_NONBLOCK, 0);
  if (fifo_fd == -1) {
    log_error("open: %s", strerror(errno));
    remove(cstr_str(&cfg.fifopath));
    return -1;
  }

  setenv("LFMFIFO", cstr_str(&cfg.fifopath), 1);

  ev_io_init(&fifo_watcher, fifo_cb, fifo_fd, EV_READ);
  fifo_watcher.data = lfm;
  ev_io_start(lfm->loop, &fifo_watcher);

  return 0;
}

void fifo_deinit(void) {
  if (fifo_fd != -1) {
    close(fifo_fd);
    remove(cstr_str(&cfg.fifopath));
    fifo_fd = -1;
  }
}

static void fifo_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;

  Lfm *lfm = w->data;

  char buf[512];
  ssize_t nread = read(w->fd, buf, sizeof buf);

  if (nread <= 0) {
    return;
  }

  if ((size_t)nread < sizeof buf) {
    llua_evaln(lfm->L, buf, nread);
  } else {
    size_t capacity = 2 * sizeof buf;
    char *dyn_buf = xmalloc(capacity);
    size_t length = nread;
    memcpy(dyn_buf, buf, nread);
    while ((nread = read(fifo_fd, dyn_buf + length, capacity - length)) > 0) {
      length += nread;
      if (length == capacity) {
        capacity *= 2;
        dyn_buf = xrealloc(dyn_buf, capacity);
      }
    }
    llua_evaln(lfm->L, dyn_buf, length);
    xfree(dyn_buf);
  }

  ev_idle_start(loop, &lfm->ui.redraw_watcher);
}
