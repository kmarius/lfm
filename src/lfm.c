#include "lfm.h"

#include "async/async.h"
#include "config.h"
#include "fifo.h"
#include "hooks.h"
#include "input.h"
#include "loader.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "macros.h"
#include "mode.h"
#include "notify.h"
#include "profiling.h"
#include "stc/common.h"
#include "stc/cstr.h"
#include "ui.h"
#include "util.h"
#include "vec_env.h"

#include <ev.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/wait.h>

struct sched_timer {
  ev_timer watcher;
  int ref;
};

#define i_declared
#define i_type list_timer, struct sched_timer
#include "stc/dlist.h"

// ev_io wrapper for stdout/stderr
struct out_watcher {
  ev_io w;
  FILE *stream;
  int ref;
};

// ev_child wrapper for child processes with stdout/err
struct child_watcher {
  ev_child w;
  struct out_watcher wstdout; // valid if .stream != NULL
  struct out_watcher wstderr; // valid if .stream != NULL
  int ref;                    // ref to lua callback
};

static inline void destroy_child_watcher(struct child_watcher *w);

#define i_declared
#define i_type list_child, struct child_watcher
#define i_keydrop(p) (destroy_child_watcher(p))
#define i_no_clone
#include "stc/dlist.h"

// watcher and corresponding stdout/-err watchers need to be stopped before
// calling this function
static inline void destroy_child_watcher(struct child_watcher *w) {
  if (w) {
    Lfm *lfm = w->w.data;
    if (w->wstdout.stream) {
      if (w->wstdout.ref)
        llua_run_stdout_callback(lfm->L, w->wstdout.ref, NULL, 0);
      fclose(w->wstdout.stream);
    }
    if (w->wstderr.stream) {
      if (w->wstderr.ref)
        llua_run_stdout_callback(lfm->L, w->wstderr.ref, NULL, 0);
      fclose(w->wstderr.stream);
    }
  }
}

static void child_cb(EV_P_ ev_child *w, int revents) {
  (void)revents;

  struct child_watcher *child = (struct child_watcher *)w;
  Lfm *lfm = w->data;

  if (child->wstdout.stream) {
    ev_invoke(EV_A_ & child->wstdout.w, 0);
    ev_io_stop(EV_A_ & child->wstdout.w);
  }

  if (child->wstderr.stream) {
    ev_invoke(EV_A_ & child->wstderr.w, 0);
    ev_io_stop(EV_A_ & child->wstderr.w);
  }

  if (child->ref) {
    llua_run_child_callback(lfm->L, child->ref, WEXITSTATUS(w->rstatus));
  }

  ev_child_stop(EV_A_ w);

  list_child_erase_node(&lfm->child_watchers, list_child_get_node(child));
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void child_out_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;

  struct out_watcher *data = (struct out_watcher *)w;
  Lfm *lfm = w->data;

  char *line = NULL;
  int read;
  size_t n;

  while ((read = getline(&line, &n, data->stream)) != -1) {
    if (line[read - 1] == '\n') {
      read--;
    }

    if (data->ref) {
      llua_run_stdout_callback(lfm->L, data->ref, line, read);
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

static void schedule_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  struct sched_timer *timer = (struct sched_timer *)w;
  Lfm *lfm = w->data;
  ev_timer_stop(EV_A_ w);
  llua_run_callback(lfm->L, timer->ref);
  list_timer_erase_node(&lfm->schedule_timers, list_timer_get_node(timer));
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

// To run command line cmds after loop starts. I think it is called back before
// every other cb.
static void prepare_cb(EV_P_ ev_prepare *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;

  vec_zsview commands = vec_zsview_move(&lfm->opts.commands);
  c_foreach(it, vec_zsview, commands) {
    llua_eval_zsview(lfm->L, *it.ref);
  }
  vec_zsview_drop(&commands);

  vec_message messages = vec_message_move(&lfm->messages);
  c_foreach(it, vec_message, messages) {
    if (it.ref->error) {
      lfm_error(lfm, "%s", it.ref->text);
    } else {
      lfm_print(lfm, "%s", it.ref->text);
    }
  }
  vec_message_drop(&messages);

  lfm_run_hook(lfm, LFM_HOOK_ENTER);
  ev_prepare_stop(EV_A_ w);
}

static void sigtstp_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGTSTP");
  ev_signal_stop(loop, w);
  ui_suspend(&lfm->ui);
  raise(SIGTSTP);
  ui_resume(&lfm->ui);
  ui_redraw(&lfm->ui, REDRAW_FULL);
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
  ev_signal_start(loop, w);
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

static void sigpipe_cb(EV_P_ ev_signal *w, int revents) {
  (void)revents;
  (void)loop;
  (void)w;
  // the only source of sigpipe that i have seen is the rpc server string
  // to send a response to a disconnected peer (e.g. after exiting a foreground
  // program)
  log_error("received SIGHUP");
}

static inline void init_dirs(Lfm *lfm) {
  (void)lfm;
  if (mkdir_p(cstr_data(&cfg.rundir), 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (mkdir_p(cstr_data(&cfg.statedir), 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (mkdir_p(cstr_data(&cfg.cachedir), 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

static inline void setup_signal_handlers(Lfm *lfm) {
  log_trace("installing signals handlers");

  // Runs only once, executes commands passed via command line, prints messages,
  // runs the LfmEnter hook
  ev_prepare_init(&lfm->prepare_watcher, prepare_cb);
  lfm->prepare_watcher.data = lfm;
  ev_prepare_start(lfm->loop, &lfm->prepare_watcher);

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

  ev_signal_init(&lfm->sigpipe_watcher, sigpipe_cb, SIGPIPE);
  lfm->sigpipe_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigpipe_watcher);

  ev_signal_init(&lfm->sigtstp_watcher, sigtstp_cb, SIGTSTP);
  lfm->sigtstp_watcher.data = lfm;
  ev_signal_start(lfm->loop, &lfm->sigtstp_watcher);
}

static inline void init_loop(Lfm *lfm) {
  lfm->loop = ev_default_loop(EVFLAG_NOENV);
}

void lfm_init(Lfm *lfm, struct lfm_opts *opts) {
  memset(lfm, 0, sizeof *lfm);
  lfm->opts = *opts;

  init_loop(lfm);
  init_dirs(lfm);
  fifo_init(lfm);

  /* notify should be available on fm startup */
  notify_init(&lfm->notify);
  loader_init(&lfm->loader);
  async_init(&lfm->async);
  PROFILE("fm_init", { fm_init(&lfm->fm, &lfm->opts); });
  PROFILE("ui_init", { ui_init(&lfm->ui); });
  setup_signal_handlers(lfm);
  lfm_hooks_init(lfm);
  lfm_modes_init(lfm);

  // Initialize lua state, we need to run some hooks that could not run during
  // fm initialization.
  PROFILE("lua_init", { lfm_lua_init(lfm); });
  c_foreach(v, dircache, lfm->loader.dc) {
    lfm_run_hook(lfm, LFM_HOOK_DIRLOADED, dir_path(v.ref->second));
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
  fifo_deinit();

  cstr_drop(&lfm->opts.startfile);
  cstr_drop(&lfm->opts.startpath);
}

int lfm_run(Lfm *lfm) {
  ev_run(lfm->loop, 0);
  return lfm->ret;
}

void lfm_quit(Lfm *lfm, int ret) {
  lfm_run_hook(lfm, LFM_HOOK_EXITPRE, ret);
  ev_break(lfm->loop, EVBREAK_ALL);
  // prevent lua error from flashing in the UI, we use it to immediately give
  // back control to the host program.
  lfm->ui.running = false;
  lfm->ret = ret;

  if (lfm->opts.lastdir_path) {
    FILE *fp = fopen(lfm->opts.lastdir_path, "w");
    if (fp == NULL) {
      log_error("lastdir: %s", strerror(errno));
      return;
    }
    fwrite(cstr_str(&lfm->fm.pwd), 1, cstr_size(&lfm->fm.pwd), fp);
    fclose(fp);
  }
}

void lfm_on_resize(Lfm *lfm) {
  ui_on_resize(&lfm->ui);
  fm_on_resize(&lfm->fm, lfm->ui.y - 2);
  lfm_run_hook(lfm, LFM_HOOK_RESIZED);
}

static void init_io_watcher(struct out_watcher *data, Lfm *lfm, int fd,
                            int ref) {
  if (fd == -1) {
    return;
  }

  FILE *file = fdopen(fd, "r");
  if (file == NULL) {
    log_error("fdopen: %s", strerror(errno));
    close(fd);
    return;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  data->w.data = lfm;
  data->stream = file;
  data->ref = ref;

  ev_io_init(&data->w, child_out_cb, fd, EV_READ);
  ev_io_start(lfm->loop, &data->w);
}

// spawn a background program
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args, vec_env *env,
              const vec_bytes *stdin_lines, int *stdin_fd, bool capture_stdout,
              bool capture_stderr, int stdout_ref, int stderr_ref, int exit_ref,
              zsview working_directory) {

  bool send_stdin = stdin_lines != NULL || stdin_fd != NULL;
  capture_stdout |= stdout_ref != 0;
  capture_stderr |= stderr_ref != 0;

  int pipe_stdin[2];
  int pipe_stdout[2];
  int pipe_stderr[2];

  if (send_stdin) {
    pipe(pipe_stdin);
  }
  if (capture_stdout) {
    pipe(pipe_stdout);
  }
  if (capture_stderr) {
    pipe(pipe_stderr);
  }

  int pid = fork();
  if (unlikely(pid < 0)) {
    if (send_stdin) {
      close(pipe_stdin[0]);
      close(pipe_stdin[1]);
    }
    if (capture_stdout) {
      close(pipe_stdout[0]);
      close(pipe_stdout[1]);
    }
    if (capture_stderr) {
      close(pipe_stderr[0]);
      close(pipe_stderr[1]);
    }
    lfm_error(lfm, "fork: %s", strerror(errno));
    return -1;
  }

  if (pid == 0) {
    // child

    if (env) {
      c_foreach(n, vec_env, *env) {
        vec_env_raw v = vec_env_value_toraw(n.ref);
        setenv(v.key, v.val, 1);
      }
    }

    if (send_stdin) {
      close(pipe_stdin[1]);
      dup2(pipe_stdin[0], 0);
      close(pipe_stdin[0]);
    }

    if (capture_stdout) {
      close(pipe_stdout[0]);
      dup2(pipe_stdout[1], 1);
      close(pipe_stdout[1]);
    } else {
      int devnull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
      dup2(devnull, 1);
      close(devnull);
    }

    if (capture_stderr) {
      close(pipe_stderr[0]);
      dup2(pipe_stderr[1], 2);
      close(pipe_stderr[1]);
    } else {
      int devnull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
      dup2(devnull, 2);
      close(devnull);
    }

    if (!zsview_is_empty(working_directory)) {
      if (chdir(working_directory.str) != 0) {
        if (capture_stderr) {
          char buf[128];
          int len = snprintf(buf, sizeof buf - 1, "chdir: %s", strerror(errno));
          write(2, buf, len);
        }
        _exit(1);
      }
    }

    execvp(prog, (char **)args);
    log_error("execvp: %s", strerror(errno));
    if (capture_stderr) {
      char buf[128];
      int len = snprintf(buf, sizeof buf - 1, "execvp: %s", strerror(errno));
      write(2, buf, len);
    }
    _exit(ENOSYS);
  }

  if (exit_ref != 0 || capture_stdout || capture_stderr) {
    struct child_watcher *data =
        list_child_push(&lfm->child_watchers, (struct child_watcher){
                                                  .ref = exit_ref,
                                              });
    if (capture_stdout) {
      close(pipe_stdout[1]);
      init_io_watcher(&data->wstdout, lfm, pipe_stdout[0], stdout_ref);
    }
    if (capture_stderr) {
      close(pipe_stderr[1]);
      init_io_watcher(&data->wstderr, lfm, pipe_stderr[0], stderr_ref);
    }
    ev_child_init(&data->w, child_cb, pid, 0);
    data->w.data = lfm;
    ev_child_start(lfm->loop, &data->w);
  }

  if (send_stdin) {
    close(pipe_stdin[0]);
    if (stdin_lines) {
      c_foreach(it, vec_bytes, *stdin_lines) {
        write(pipe_stdin[1], it.ref->buf, it.ref->size);
        write(pipe_stdin[1], "\n", 1);
      }
    }
    if (stdin_fd) {
      *stdin_fd = pipe_stdin[1];
    } else {
      close(pipe_stdin[1]);
    }
  }

  return pid;
}

// execute a foreground program
int lfm_execute(Lfm *lfm, const char *prog, char *const *args, vec_env *env,
                vec_bytes *stdin_lines, vec_bytes *stdout_lines,
                vec_bytes *stderr_lines) {
  int status, rc;
  lfm_run_hook(lfm, LFM_HOOK_EXECPRE);
  ev_signal_stop(lfm->loop, &lfm->sigint_watcher);
  ev_signal_stop(lfm->loop, &lfm->sigtstp_watcher);
  ui_suspend(&lfm->ui);

  bool capture_stdout = stdout_lines != NULL;
  bool capture_stderr = stderr_lines != NULL;
  bool send_stdin = stdin_lines != NULL;

  int pipe_stdout[2];
  int pipe_stderr[2];
  int pipe_stdin[2];

  if (send_stdin) {
    pipe(pipe_stdin);
  }
  if (capture_stdout) {
    pipe(pipe_stdout);
  }
  if (capture_stderr) {
    pipe(pipe_stderr);
  }

  int pid = fork();
  if (unlikely(pid < 0)) {
    if (send_stdin) {
      close(pipe_stdin[0]);
      close(pipe_stdin[1]);
    }
    if (capture_stdout) {
      close(pipe_stdout[0]);
      close(pipe_stdout[1]);
    }
    if (capture_stderr) {
      close(pipe_stderr[0]);
      close(pipe_stderr[1]);
    }
    ui_resume(&lfm->ui);
    ev_signal_start(lfm->loop, &lfm->sigint_watcher);
    ev_signal_start(lfm->loop, &lfm->sigtstp_watcher);
    lfm_run_hook(lfm, LFM_HOOK_EXECPOST);
    lfm_error(lfm, "fork: %s", strerror(errno));
    return -1;
  }

  if (pid == 0) {
    // child

    if (env) {
      c_foreach(n, vec_env, *env) {
        vec_env_raw v = vec_env_value_toraw(n.ref);
        setenv(v.key, v.val, 1);
      }
    }

    signal(SIGINT, SIG_DFL);
    if (chdir(cstr_str(&lfm->fm.pwd)) != 0) {
      fprintf(stderr, "chdir: %s\n", strerror(errno));
      _exit(1);
    }

    if (send_stdin) {
      close(pipe_stdin[1]);
      dup2(pipe_stdin[0], 0);
      close(pipe_stdin[0]);
    }

    if (capture_stdout) {
      close(pipe_stdout[0]);
      dup2(pipe_stdout[1], 1);
      close(pipe_stdout[1]);
    }

    if (capture_stderr) {
      close(pipe_stderr[0]);
      dup2(pipe_stderr[1], 2);
      close(pipe_stderr[1]);
    }

    execvp(prog, (char *const *)args);
    log_error("execvp: %s", strerror(errno));
    if (capture_stderr) {
      char buf[128];
      int len = snprintf(buf, sizeof buf - 1, "execvp: %s", strerror(errno));
      write(2, buf, len);
    }
    _exit(127);
  }

  signal(SIGINT, SIG_IGN);
  FILE *file_stderr = NULL;
  FILE *file_stdout = NULL;

  if (capture_stdout) {
    close(pipe_stdout[1]);
    file_stdout = fdopen(pipe_stdout[0], "r");
    if (file_stdout == NULL) {
      log_error("fdopen: %s", strerror(errno));
      close(pipe_stdout[0]);
    }
  }

  if (capture_stderr) {
    close(pipe_stderr[1]);
    file_stderr = fdopen(pipe_stderr[0], "r");
    if (file_stderr == NULL) {
      log_error("fdopen: %s", strerror(errno));
      close(pipe_stderr[0]);
    }
  }

  if (send_stdin) {
    log_trace("sending stdin");
    close(pipe_stdin[0]);
    c_foreach(it, vec_bytes, *stdin_lines) {
      write(pipe_stdin[1], it.ref->buf, it.ref->size);
      write(pipe_stdin[1], "\n", 1);
    }
    close(pipe_stdin[1]);
  }

  // as is the case with previews, we probably have to drain stdout/stderr or
  // the process might not finish

  log_trace("waiting for process %d to finish", pid);
  do {
    rc = waitpid(pid, &status, 0);
  } while ((rc == -1) && (errno == EINTR));
  log_trace("process %d finished with status %d", pid, WEXITSTATUS(status));

  ui_resume(&lfm->ui);
  ev_signal_start(lfm->loop, &lfm->sigint_watcher);
  ev_signal_start(lfm->loop, &lfm->sigtstp_watcher);
  lfm_run_hook(lfm, LFM_HOOK_EXECPOST);

  if (capture_stdout && file_stdout != NULL) {
    log_trace("reading stdout");

    char *line = NULL;
    int read;
    size_t n;

    while ((read = getline(&line, &n, file_stdout)) != -1) {
      if (line[read - 1] == '\n') {
        read--;
      }
      vec_bytes_push_back(stdout_lines, bytes_from_n(line, read));
    }
    free(line);

    fclose(file_stdout);
  }

  if (capture_stderr && file_stderr != NULL) {
    log_trace("reading stderr");

    char *line = NULL;
    int read;
    size_t n;

    while ((read = getline(&line, &n, file_stderr)) != -1) {
      if (line[read - 1] == '\n') {
        read--;
      }
      vec_bytes_push_back(stderr_lines, bytes_from_n(line, read));
    }
    free(line);

    fclose(file_stderr);
  }

  if (status == -1) {
    // if commands return this, we need different error signalling
    lfm_error(lfm, "command returned -1");
  }

  return WEXITSTATUS(status);
}

void lfm_print(Lfm *lfm, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  if (!lfm->ui.running) {
    struct message msg = {};
    cstr_vfmt(&msg.text, 0, fmt, args);
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_vechom(&lfm->ui, fmt, args);
  }

  va_end(args);
}

void lfm_error(Lfm *lfm, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  if (!lfm->ui.running) {
    struct message msg = {.error = true};
    cstr_vfmt(&msg.text, 0, fmt, args);
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_verror(&lfm->ui, fmt, args);
  }

  va_end(args);
}

void lfm_schedule(Lfm *lfm, int ref, uint32_t delay) {
  struct sched_timer *data =
      list_timer_push(&lfm->schedule_timers, (struct sched_timer){
                                                 .ref = ref,
                                             });
  ev_timer_init(&data->watcher, schedule_timer_cb, 1.0 * delay / 1000, 0);
  data->watcher.data = lfm;
  ev_timer_start(lfm->loop, &data->watcher);
}
