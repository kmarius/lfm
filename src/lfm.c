#include "lfm.h"

#include "async.h"
#include "config.h"
#include "hooks.h"
#include "input.h"
#include "loader.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "mode.h"
#include "notify.h"
#include "popen_arr.h"
#include "stc/common.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

struct sched_timer {
  ev_timer watcher;
  Lfm *lfm;
  int ref;
};

#define i_is_forward
#define i_type list_timer
#define i_val struct sched_timer
#include "stc/dlist.h"

struct child_watcher {
  ev_child watcher;
  Lfm *lfm;
  int ref;
  ev_io *stdout_watcher;
  ev_io *stderr_watcher;
};

static inline void destroy_io_watcher(ev_io *w);

#define i_is_forward
#define i_type list_child
#define i_val struct child_watcher
#define i_valdrop(p)                                                           \
  (destroy_io_watcher(p->stdout_watcher), destroy_io_watcher(p->stderr_watcher))
#define i_no_clone
#include "stc/dlist.h"

#define TICK 1 // heartbeat, in seconds

// Size of the buffer for reading from the fifo. Switches to a dynamic buffer if
// full.
#define FIFO_BUF_SZ 512

struct stdout_watcher_data {
  ev_io watcher;
  Lfm *lfm;
  FILE *stream;
  int ref;
};

// watcher and corresponding stdout/-err watchers need to be stopped before
// calling this function
static inline void destroy_io_watcher(ev_io *w) {
  if (!w) {
    return;
  }
  struct stdout_watcher_data *data = (struct stdout_watcher_data *)w;
  if (data->ref) {
    llua_run_stdout_callback(data->lfm->L, data->ref, NULL);
  }
  fclose(data->stream);
  xfree(data);
}

static void fifo_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;
  (void)loop;

  Lfm *lfm = w->data;

  char buf[FIFO_BUF_SZ];
  ssize_t nread = read(w->fd, buf, sizeof buf);

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

static void child_cb(EV_P_ ev_child *w, int revents) {
  (void)revents;
  struct child_watcher *watcher = (struct child_watcher *)w;
  Lfm *lfm = watcher->lfm;

  if (watcher->ref) {
    llua_run_child_callback(lfm->L, watcher->ref, WEXITSTATUS(w->rstatus));
  }

  if (watcher->stdout_watcher) {
    watcher->stdout_watcher->cb(EV_A_ watcher->stdout_watcher, 0);
    ev_io_stop(EV_A_ watcher->stdout_watcher);
  }

  if (watcher->stderr_watcher) {
    watcher->stderr_watcher->cb(EV_A_ watcher->stderr_watcher, 0);
    ev_io_stop(EV_A_ watcher->stderr_watcher);
  }

  ev_child_stop(EV_A_ w);

  list_child_erase_node(&lfm->child_watchers, list_child_get_node(watcher));
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void schedule_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct sched_timer *timer = (struct sched_timer *)w;
  Lfm *lfm = timer->lfm;
  ev_timer_stop(EV_A_ w);
  llua_run_callback(lfm->L, timer->ref);
  list_timer_erase_node(&lfm->schedule_timers, list_timer_get_node(timer));
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  (void)lfm;
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

    if (data->ref) {
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

  c_foreach(it, vec_str, cfg.commands) {
    llua_eval(lfm->L, *it.ref);
  }
  vec_str_drop(&cfg.commands);
  cfg.commands = vec_str_init();

  c_foreach(it, vec_message, lfm->messages) {
    if (it.ref->error) {
      lfm_error(lfm, "%s", it.ref->text);
    } else {
      lfm_print(lfm, "%s", it.ref->text);
    }
  }
  vec_message_drop(&lfm->messages);

  lfm_run_hook(lfm, LFM_HOOK_ENTER);
  ev_prepare_stop(EV_A_ w);
}

static void sigint_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGINT");
  input_handle_key(lfm, CTRL('C'));
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

// unclear if this happens before/after resizecb is called by notcurses
static void sigwinch_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGWINCH");
  ui_clear(&lfm->ui);
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void sigterm_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  (void)loop;
  log_trace("received SIGTERM");
  lfm_quit(w->data, 0);
}

static void sighup_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  (void)loop;
  log_trace("received SIGHUP");
  lfm_quit(w->data, 0);
}

static inline void init_dirs(Lfm *lfm) {
  (void)lfm;
  if (mkdir_p(cfg.rundir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (mkdir_p(cfg.statedir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (mkdir_p(cfg.cachedir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static inline void init_fifo(Lfm *lfm) {
  if ((mkfifo(cfg.fifopath, 0600) == -1 && errno != EEXIST)) {
    fprintf(stderr, "mkfifo: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((lfm->fifo_fd = open(cfg.fifopath, O_RDWR | O_NONBLOCK, 0)) == -1) {
    fprintf(stderr, "open: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  setenv("LFMFIFO", cfg.fifopath, 1);

  ev_io_init(&lfm->fifo_watcher, fifo_cb, lfm->fifo_fd, EV_READ);
  lfm->fifo_watcher.data = lfm;
  ev_io_start(lfm->loop, &lfm->fifo_watcher);
}

static inline void init_loop(Lfm *lfm) {
  lfm->loop = ev_default_loop(EVFLAG_NOENV);

  // Runs only once, executes commands passed via command line, prints messages,
  // runs the LfmEnter hook
  ev_prepare_init(&lfm->prepare_watcher, prepare_cb);
  lfm->prepare_watcher.data = lfm;
  ev_prepare_start(lfm->loop, &lfm->prepare_watcher);

  // Heartbeat, currently does nothing
  ev_timer_init(&lfm->timer_watcher, timer_cb, TICK, TICK);
  lfm->timer_watcher.data = lfm;

  // Catch some signals
  ev_signal_init(&lfm->sigint_watcher, sigint_cb, SIGINT);
  lfm->sigint_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigint_watcher);

  ev_signal_init(&lfm->sigwinch_watcher, sigwinch_cb, SIGWINCH);
  lfm->sigwinch_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigwinch_watcher);

  ev_signal_init(&lfm->sigterm_watcher, sigterm_cb, SIGTERM);
  lfm->sigterm_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigterm_watcher);

  ev_signal_init(&lfm->sighup_watcher, sighup_cb, SIGHUP);
  lfm->sighup_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sighup_watcher);
}

void lfm_init(Lfm *lfm, FILE *log) {
  memset(lfm, 0, sizeof *lfm);
  lfm->log_fp = log;

  init_loop(lfm);
  init_dirs(lfm);
  init_fifo(lfm);

  /* notify should be available on fm startup */
  notify_init(&lfm->notify);
  loader_init(&lfm->loader);
  async_init(&lfm->async);
  fm_init(&lfm->fm);
  ui_init(&lfm->ui);
  lfm_hooks_init(lfm);
  lfm_modes_init(lfm);

  // Initialize lua state, we need to run some hooks that could not run during
  // fm initialization.
  lfm_lua_init(lfm);
  c_foreach(v, dircache, lfm->loader.dc) {
    lfm_run_hook1(lfm, LFM_HOOK_DIRLOADED, v.ref->second->path);
  }
}

void lfm_deinit(Lfm *lfm) {
  lfm_modes_deinit(lfm);
  list_child_drop(&lfm->child_watchers);
  list_timer_drop(&lfm->schedule_timers);
  notify_deinit(&lfm->notify);
  ui_deinit(&lfm->ui);
  fm_deinit(&lfm->fm);
  lfm_hooks_deinit(lfm);
  loader_deinit(&lfm->loader);
  lfm_lua_deinit(lfm);
  async_deinit(&lfm->async);
  close(lfm->fifo_fd);
  remove(cfg.fifopath);
}

int lfm_run(Lfm *lfm) {
  ev_run(lfm->loop, 0);
  return lfm->ret;
}

void lfm_quit(Lfm *lfm, int ret) {
  lfm_run_hook(lfm, LFM_HOOK_EXITPRE);
  ev_break(lfm->loop, EVBREAK_ALL);
  // prevent lua error from flashing in the UI, we use it to immediately give
  // back control to the host program.
  lfm->ui.running = false;
  lfm->ret = ret;

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

// spawn a background program
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args,
              const vec_str *stdin_lines, bool out, bool err, int stdout_ref,
              int stderr_ref, int exit_ref) {
  FILE *stdin_stream, *stdout_stream, *stderr_stream;

  bool capture_stdout = out || stdout_ref;
  bool capture_stderr = err || stderr_ref;

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
    c_foreach(it, vec_str, *stdin_lines) {
      fputs(*it.ref, stdin_stream);
      fputc('\n', stdin_stream);
    }
    fclose(stdin_stream);
  }

  struct child_watcher *data = list_child_push(
      &lfm->child_watchers, (struct child_watcher){
                                .ref = exit_ref,
                                .lfm = lfm,
                                .stdout_watcher = stdout_watcher,
                                .stderr_watcher = stderr_watcher,
                            });
  ev_child_init(&data->watcher, child_cb, pid, 0);
  ev_child_start(lfm->loop, &data->watcher);

  return pid;
}

// execute a foreground program
bool lfm_execute(Lfm *lfm, const char *prog, char *const *args) {
  int pid, status, rc;
  lfm_run_hook(lfm, LFM_HOOK_EXECPRE);
  ev_signal_stop(lfm->loop, &lfm->sigint_watcher);
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
  ev_signal_start(lfm->loop, &lfm->sigint_watcher);
  lfm_run_hook(lfm, LFM_HOOK_EXECPOST);
  return status == 0;
}

void lfm_print(Lfm *lfm, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (!lfm->ui.running) {
    struct message msg = {NULL, false};
    vasprintf(&msg.text, format, args);
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_vechom(&lfm->ui, format, args);
  }

  va_end(args);
}

void lfm_error(Lfm *lfm, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (!lfm->ui.running) {
    struct message msg = {NULL, true};
    vasprintf(&msg.text, format, args);
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_verror(&lfm->ui, format, args);
  }

  va_end(args);
}

void lfm_schedule(Lfm *lfm, int ref, uint32_t delay) {
  struct sched_timer *data =
      list_timer_push(&lfm->schedule_timers, (struct sched_timer){
                                                 .lfm = lfm,
                                                 .ref = ref,
                                             });
  ev_timer_init(&data->watcher, schedule_timer_cb, 1.0 * delay / 1000, 0);
  ev_timer_start(lfm->loop, &data->watcher);
}
