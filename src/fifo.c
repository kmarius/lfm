#include "fifo.h"

#include "config.h"
#include "lfm.h"
#include "log.h"
#include "loop.h"

#include <ev.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static ev_io fifo_watcher;
static i32 fifo_fd = -1;

static void fifo_cb(EV_P_ ev_io *w, i32 revents);

i32 fifo_init(Lfm *lfm) {
  log_trace("setting up fifo");

  if (mkfifo(cstr_str(&cfg.fifopath), 0600) == -1 && errno != EEXIST) {
    log_perror("mkfifo");
    return -1;
  }

  fifo_fd = open(cstr_str(&cfg.fifopath), O_RDWR | O_NONBLOCK, 0);
  if (fifo_fd == -1) {
    log_perror("open");
    remove(cstr_str(&cfg.fifopath));
    return -1;
  }

  setenv("LFMFIFO", cstr_str(&cfg.fifopath), 1);

  ev_io_init(&fifo_watcher, fifo_cb, fifo_fd, EV_READ);
  fifo_watcher.data = lfm;
  ev_io_start(event_loop, &fifo_watcher);

  return 0;
}

void fifo_deinit(void) {
  if (fifo_fd != -1) {
    close(fifo_fd);
    remove(cstr_str(&cfg.fifopath));
    fifo_fd = -1;
  }
}

static void fifo_cb(EV_P_ ev_io *w, i32 revents) {
  (void)revents;

  Lfm *lfm = w->data;

  char buf[4];
  char *buf1 = buf;
  isize nread = read(w->fd, buf, sizeof buf);

  if (nread <= 0)
    return;

  usize length = nread;
  if (length == sizeof buf) {
    usize capacity = 2 * sizeof buf;
    buf1 = xmalloc(capacity);
    memcpy(buf1, buf, sizeof buf);
    while ((nread = read(fifo_fd, buf1 + length, capacity - length)) > 0) {
      length += nread;
      if (length == capacity) {
        capacity *= 2;
        buf1 = xrealloc(buf1, capacity);
      }
    }
  }
  lfm_lua_evaln(lfm->L, buf1, length);
  if (buf1 != buf)
    xfree(buf1);
}
