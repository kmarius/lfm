#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "hashtab.h"
#include "hooks.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "mode.h"
#include "notify.h"
#include "popen_arr.h"
#include "ui.h"
#include "util.h"

#define TICK 1 // in seconds

// Size of the buffer for reading from the fifo. Switches to a dynamic buffer if
// full.
#define FIFO_BUF_SZ 512

// callbacks {{{

// child watchers {{{

struct stdout_watcher_data {
  ev_io watcher;
  Lfm *lfm;
  FILE *stream;
  int ref;
};

struct child_watcher_data {
  ev_child watcher;
  Lfm *lfm;
  int ref;
  ev_io *stdout_watcher;
  ev_io *stderr_watcher;
};

// watcher and corresponding stdout/-err watchers need to be stopped before
// calling this function
static inline void destroy_io_watcher(ev_io *w) {
  if (!w) {
    return;
  }
  struct stdout_watcher_data *data = (struct stdout_watcher_data *)w;
  if (data->ref >= 0) {
    llua_run_stdout_callback(data->lfm->L, data->ref, NULL);
  }
  fclose(data->stream);
  xfree(data);
}

static inline void destroy_child_watcher(ev_child *w) {
  if (!w) {
    return;
  }
  struct child_watcher_data *data = (struct child_watcher_data *)w;
  destroy_io_watcher(data->stdout_watcher);
  destroy_io_watcher(data->stderr_watcher);
  xfree(data);
}

static void child_cb(EV_P_ ev_child *w, int revents) {
  (void)revents;
  struct child_watcher_data *data = (struct child_watcher_data *)w;

  ev_child_stop(EV_A_ w);

  if (data->ref >= 0) {
    llua_run_child_callback(data->lfm->L, data->ref, w->rstatus);
  }

  if (data->stdout_watcher) {
    data->stdout_watcher->cb(EV_A_ data->stdout_watcher, 0);
    ev_io_stop(EV_A_ data->stdout_watcher);
  }

  if (data->stderr_watcher) {
    data->stderr_watcher->cb(EV_A_ data->stderr_watcher, 0);
    ev_io_stop(EV_A_ data->stderr_watcher);
  }

  ev_idle_start(EV_A_ & data->lfm->ui.redraw_watcher);
  cvector_swap_remove(data->lfm->child_watchers, w);
  destroy_child_watcher(w);
}

// }}}

// scheduling timers {{{

struct schedule_timer_data {
  ev_timer watcher;
  Lfm *lfm;
  int ref;
};

static void schedule_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct schedule_timer_data *data = (struct schedule_timer_data *)w;
  ev_timer_stop(EV_A_ w);
  llua_run_callback(data->lfm->L, data->ref);
  cvector_swap_remove(data->lfm->schedule_timers, w);
  ev_idle_start(EV_A_ & data->lfm->ui.redraw_watcher);
  free(w);
}

// }}}

static void timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  /* Lfm *lfm = w->data; */
  ev_timer_stop(EV_A_ w);
}

static void command_stdout_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;
  struct stdout_watcher_data *data = (struct stdout_watcher_data *)w;
  Lfm *lfm = data->lfm;

  char *line = NULL;
  int read;
  size_t n;

  while ((read = getline(&line, &n, data->stream)) != -1) {
    if (line[read - 1] == '\n') {
      line[read - 1] = 0;
    }

    if (data->ref >= 0) {
      llua_run_stdout_callback(lfm->L, data->ref, line);
    } else {
      ui_echom(&lfm->ui, "%s", line);
    }
  }
  xfree(line);

  // this seems to prevent the callback being immediately called again by libev
  if (errno == EAGAIN) {
    clearerr(data->stream);
  }

  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

// To run command line cmds after loop starts. I think it is called back before
// every other cb.
static void prepare_cb(EV_P_ ev_prepare *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;

  if (cfg.commands) {
    cvector_foreach(const char *cmd, cfg.commands) {
      llua_eval(lfm->L, cmd);
    }
    cvector_free_clear(cfg.commands);
  }

  if (lfm->messages) {
    cvector_foreach_ptr(struct message_s * m, lfm->messages) {
      if (m->error) {
        lfm_error(lfm, "%s", m->text);
      } else {
        lfm_print(lfm, "%s", m->text);
      }
      xfree(m->text);
    }
    cvector_free_clear(lfm->messages);
  }

  lfm_run_hook(lfm, LFM_HOOK_ENTER);
  ev_prepare_stop(EV_A_ w);
}

// unclear if this happens before/after resizecb is called by notcurses
static void sigwinch_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_debug("received SIGWINCH");
  ui_clear(&lfm->ui);
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void sigterm_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  (void)loop;
  log_debug("received SIGTERM");
  lfm_quit(w->data);
}

static void sighup_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  (void)loop;
  log_debug("received SIGHUP");
  lfm_quit(w->data);
}
/* callbacks }}} */

void lfm_init(Lfm *lfm, FILE *log_fp) {
  memset(lfm, 0, sizeof *lfm);

  lfm->log_fp = log_fp;

  lfm->loop = ev_default_loop(EVFLAG_NOENV);

  if (mkdir_p(cfg.rundir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (mkdir_p(cfg.statedir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if ((mkfifo(cfg.fifopath, 0600) == -1 && errno != EEXIST) ||
      (lfm->fifo_fd = open(cfg.fifopath, O_RDONLY | O_NONBLOCK, 0)) == -1) {
    log_error("fifo: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  setenv("LFMFIFO", cfg.fifopath, 1);

  if (mkdir_p(cfg.cachedir, 0700) == -1 && errno != EEXIST) {
    log_error("fifo: %s", strerror(errno));
  }

  /* notify should be available on fm startup */
  if (!notify_init(&lfm->notify)) {
    log_error("inotify: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  loader_init(&lfm->loader);
  async_init(&lfm->async);
  fm_init(&lfm->fm);
  ui_init(&lfm->ui);
  lfm_hooks_init(lfm);
  lfm_modes_init(lfm);

  ev_prepare_init(&lfm->prepare_watcher, prepare_cb);
  lfm->prepare_watcher.data = lfm;
  ev_prepare_start(lfm->loop, &lfm->prepare_watcher);

  ev_timer_init(&lfm->timer_watcher, timer_cb, TICK, TICK);
  lfm->timer_watcher.data = lfm;
  ev_timer_start(lfm->loop, &lfm->timer_watcher);

  signal(SIGINT, SIG_IGN);

  ev_signal_init(&lfm->sigwinch_watcher, sigwinch_cb, SIGWINCH);
  lfm->sigwinch_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigwinch_watcher);

  ev_signal_init(&lfm->sigterm_watcher, sigterm_cb, SIGTERM);
  lfm->sigterm_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigterm_watcher);

  ev_signal_init(&lfm->sighup_watcher, sighup_cb, SIGHUP);
  lfm->sighup_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sighup_watcher);

  lfm->L = luaL_newstate();
  ev_set_userdata(lfm->loop, lfm->L);
  llua_init(lfm->L, lfm);
  // can't run these hooks in the loader before initialization
  ht_foreach(Dir * dir, lfm->loader.dir_cache) {
    lfm_run_hook1(lfm, LFM_HOOK_DIRLOADED, dir->path);
  }
}

void lfm_run(Lfm *lfm) {
  ev_run(lfm->loop, 0);
}

void lfm_quit(Lfm *lfm) {
  lfm_run_hook(lfm, LFM_HOOK_EXITPRE);
  ev_break(lfm->loop, EVBREAK_ALL);
  // prevent lua error from flashing in the UI, we use it to immediately give
  // back control to the host program.
  lfm->ui.running = false;

  if (cfg.lastdir) {
    FILE *fp = fopen(cfg.lastdir, "w");
    if (!fp) {
      log_error("lastdir: %s", strerror(errno));
    } else {
      fputs(lfm->fm.pwd, fp);
      fclose(fp);
    }
  }
}

static ev_io *add_io_watcher(Lfm *lfm, FILE *f, int ref) {
  if (!f) {
    return NULL;
  }

  const int fd = fileno(f);
  if (fd < 0) {
    log_error("fileno: %s", strerror(errno));
    fclose(f);
    return NULL;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  struct stdout_watcher_data *data = xmalloc(sizeof *data);
  data->lfm = lfm;
  data->stream = f;
  data->ref = ref;

  ev_io_init(&data->watcher, command_stdout_cb, fd, EV_READ);
  ev_io_start(lfm->loop, &data->watcher);

  return &data->watcher;
}

static void add_child_watcher(Lfm *lfm, int pid, int ref, ev_io *stdout_watcher,
                              ev_io *stderr_watcher) {
  struct child_watcher_data *data = xmalloc(sizeof *data);
  data->ref = ref;
  data->lfm = lfm;
  data->stdout_watcher = stdout_watcher;
  data->stderr_watcher = stderr_watcher;

  ev_child_init(&data->watcher, child_cb, pid, 0);
  ev_child_start(lfm->loop, &data->watcher);

  cvector_push_back(lfm->child_watchers, &data->watcher);
}

// spawn a background program
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args, char **stdin_lines,
              bool out, bool err, int stdout_ref, int stderr_ref,
              int exit_ref) {
  FILE *stdin_stream, *stdout_stream, *stderr_stream;

  bool capture_stdout = out || stdout_ref >= 0;
  bool capture_stderr = err || stderr_ref >= 0;

  // always pass out and err because popen2_arr_p doesnt close the fds
  int pid = popen2_arr_p(stdin_lines ? &stdin_stream : NULL,
                         capture_stdout ? &stdout_stream : NULL,
                         capture_stderr ? &stderr_stream : NULL, prog, args,
                         lfm->fm.pwd);

  if (pid == -1) {
    lfm_error(lfm, "popen2_arr_p: %s", strerror(errno)); // not sure if set
    return -1;
  }

  ev_io *stdout_watcher =
      capture_stdout ? add_io_watcher(lfm, stdout_stream, stdout_ref) : NULL;

  ev_io *stderr_watcher =
      capture_stderr ? add_io_watcher(lfm, stderr_stream, stderr_ref) : NULL;

  if (stdin_lines) {
    cvector_foreach(char *line, stdin_lines) {
      fputs(line, stdin_stream);
      fputc('\n', stdin_stream);
    }
    fclose(stdin_stream);
  }

  add_child_watcher(lfm, pid, exit_ref, stdout_watcher, stderr_watcher);
  return pid;
}

// execute a foreground program
bool lfm_execute(Lfm *lfm, const char *prog, char *const *args) {
  int pid, status, rc;
  ui_suspend(&lfm->ui);
  if ((pid = fork()) < 0) {
    status = -1;
  } else if (pid == 0) {
    // child
    signal(SIGINT, SIG_DFL);
    if (chdir(lfm->fm.pwd) != 0) {
      fprintf(stderr, "chdir: %s\n", strerror(errno));
      _exit(1);
    }
    execvp(prog, (char *const *)args);
    _exit(127); // execl error
  } else {
    // parent
    signal(SIGINT, SIG_IGN);
    do {
      rc = waitpid(pid, &status, 0);
    } while ((rc == -1) && (errno == EINTR));
  }

  ui_resume(&lfm->ui);
  signal(SIGINT, SIG_IGN);
  return status == 0;
}

void lfm_print(Lfm *lfm, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (!lfm->ui.running) {
    struct message_s msg = {NULL, false};
    vasprintf(&msg.text, format, args);
    cvector_push_back(lfm->messages, msg);
  } else {
    ui_vechom(&lfm->ui, format, args);
  }

  va_end(args);
}

void lfm_error(Lfm *lfm, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (!lfm->ui.running) {
    struct message_s msg = {NULL, true};
    vasprintf(&msg.text, format, args);
    cvector_push_back(lfm->messages, msg);
  } else {
    ui_verror(&lfm->ui, format, args);
  }

  va_end(args);
}

void lfm_read_fifo(Lfm *lfm) {
  char buf[FIFO_BUF_SZ];

  ssize_t nread = read(lfm->fifo_fd, buf, sizeof buf);

  if (nread <= 0) {
    return;
  }

  if ((size_t)nread < sizeof buf) {
    buf[nread - 1] = 0;
    llua_eval(lfm->L, buf);
  } else {
    size_t capacity = 2 * sizeof buf;
    char *dyn_buf = xmalloc(capacity);
    size_t length = nread;
    memcpy(dyn_buf, buf, nread);
    while ((nread = read(lfm->fifo_fd, dyn_buf + length, capacity - length)) >
           0) {
      length += nread;
      if (length == capacity) {
        capacity *= 2;
        dyn_buf = xrealloc(dyn_buf, capacity);
      }
    }
    dyn_buf[length] = 0;
    llua_eval(lfm->L, dyn_buf);
    xfree(dyn_buf);
  }

  ev_idle_start(lfm->loop, &lfm->ui.redraw_watcher);
}

void lfm_schedule(Lfm *lfm, int ref, uint32_t delay) {
  struct schedule_timer_data *data = xmalloc(sizeof *data);
  data->lfm = lfm;
  data->ref = ref;
  ev_timer_init(&data->watcher, schedule_timer_cb, 1.0 * delay / 1000, 0);
  ev_timer_start(lfm->loop, &data->watcher);
  cvector_push_back(lfm->schedule_timers, &data->watcher);
}

void lfm_deinit(Lfm *lfm) {
  lfm_modes_deinit(lfm);
  cvector_ffree(lfm->child_watchers, destroy_child_watcher);
  cvector_ffree(lfm->schedule_timers, free);
  notify_deinit(&lfm->notify);
  llua_deinit(lfm->L);
  ui_deinit(&lfm->ui);
  fm_deinit(&lfm->fm);
  lfm_hooks_deinit(lfm);
  loader_deinit(&lfm->loader);
  async_deinit(&lfm->async);
  if (lfm->fifo_fd > 0) {
    close(lfm->fifo_fd);
  }
  remove(cfg.fifopath);
}
