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

#include "app.h"
#include "async.h"
#include "config.h"
#include "cvector.h"
#include "keys.h"
#include "loader.h"
#include "log.h"
#include "lualfm.h"
#include "notify.h"
#include "popen_arr.h"
#include "util.h"

#define T App

#define APP_INITIALIZER ((App){ \
    .fifo_fd = -1, \
    })

#define TICK 1  // in seconds

static App *app = NULL;  // only needed for print/error

struct message {
  char *text;
  bool error;
};

static struct message *messages = NULL;

// callbacks {{{

// child watchers {{{

struct stdout_watcher_data {
  App *app;
  FILE *stream;
  int ind;
};

struct child_watcher_data {
  App *app;
  int cb_index;
  ev_io *stdout_watcher;
  ev_io *stderr_watcher;
};

// watcher and corresponding stdout/-err watchers need to be stopped before
// calling this function
static inline void destroy_io_watcher(ev_io *w)
{
  if (!w) {
    return;
  }

  struct stdout_watcher_data *data = w->data;
  if (data->ind) {
    lua_run_stdout_callback(data->app->L, data->ind, NULL);
  }
  fclose(data->stream);
  free(data);
  free(w);
}


static inline void destroy_child_watcher(ev_child *w)
{
  if(!w) {
    return;
  }

  struct child_watcher_data *data = w->data;
  destroy_io_watcher(data->stdout_watcher);
  destroy_io_watcher(data->stderr_watcher);
  free(data);
  free(w);
}


static void child_cb(EV_P_ ev_child *w, int revents)
{
  (void) revents;
  struct child_watcher_data *data = w->data;

  ev_child_stop(EV_A_ w);

  if (data->cb_index > 0) {
    lua_run_child_callback(data->app->L, data->cb_index, w->rstatus);
  }

  if (data->stdout_watcher) {
    ev_io_stop(loop, data->stdout_watcher);
  }

  if (data->stderr_watcher) {
    ev_io_stop(loop, data->stderr_watcher);
  }

  cvector_swap_remove(data->app->child_watchers, w);
  destroy_child_watcher(w);
}

// }}}

// scheduling timers {{{

struct schedule_timer_data {
  App *app;
  int ind;
};

static inline void destroy_schedule_timer(ev_timer *w)
{
  if (!w) {
    return;
  }

  free(w->data);
  free(w);
}


static void schedule_timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  struct schedule_timer_data *data = w->data;
  ev_timer_stop(EV_A_ w);
  lua_run_callback(data->app->L, data->ind);
  cvector_swap_remove(data->app->schedule_timers, w);
  destroy_schedule_timer(w);
  ev_idle_start(loop, &app->redraw_watcher);
}

// }}}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  /* App *app = w->data; */
  ev_timer_stop(loop, w);
}


static void stdin_cb(EV_P_ ev_io *w, int revents)
{
  (void) revents;
  App *app = w->data;
  ncinput in;

  while (notcurses_getc_nblock(app->ui.nc, &in) != (uint32_t) -1) {
    if (current_millis() <= app->input_timeout) {
      continue;
    }

    /* log_debug("id: %d, shift: %d, ctrl: %d alt %d", in.id, in.shift, in.ctrl, in.alt); */
    lua_handle_key(app->L, ncinput_to_input(&in));
  }

  ev_idle_start(loop, &app->redraw_watcher);
}


static void command_stdout_cb(EV_P_ ev_io *w, int revents)
{
  (void) revents;
  struct stdout_watcher_data *data = w->data;

  char *line = NULL;
  int read;
  size_t n;

  while ((read = getline(&line, &n, data->stream)) != -1) {
    if (line[read-1] == '\n') {
      line[read-1] = 0;
    }

    if (data->ind) {
      lua_run_stdout_callback(data->app->L, data->ind, line);
    } else {
      ui_echom(&data->app->ui, "%s", line);
    }
  }
  free(line);

  // this seems to prevent the callback being immediately called again by libev
  if (errno == EAGAIN) {
    clearerr(data->stream);
  }

  ev_idle_start(loop, &data->app->redraw_watcher);
}


// To run command line cmds after loop starts. I think it is called back before
// every other cb.
static void prepare_cb(EV_P_ ev_prepare *w, int revents)
{
  (void) revents;
  App *app = w->data;

  if (cfg.commands) {
    for (size_t i = 0; i < cvector_size(cfg.commands); i++) {
      lua_eval(app->L, cfg.commands[i]);
    }

    // commands are from argv, don't free them
    cvector_free(cfg.commands);
    cfg.commands = NULL;
  }

  if (messages) {
    cvector_foreach_ptr(m, messages) {
      if (m->error) {
        error("%s", m->text);
      } else {
        print("%s", m->text);
      }
      free(m->text);
    }
    cvector_free(messages);
  }

  lua_run_hook(app->L, LFM_HOOK_ENTER);
  ev_prepare_stop(loop, w);
}


static void sigwinch_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  App *app = w->data;
  ui_clear(&app->ui);
  lua_run_hook(app->L, LFM_HOOK_RESIZED);
  ev_idle_start(loop, &app->redraw_watcher);
}


static void sigterm_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  (void) loop;
  app_quit(w->data);
}


static void sighup_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  (void) loop;
  app_quit(w->data);
}


static void redraw_cb(EV_P_ ev_idle *w, int revents)
{
  (void) revents;
  App *app = w->data;
  ui_draw(&app->ui);
  ev_idle_stop(loop, w);
}

/* callbacks }}} */

void app_init(T *t)
{
  app = t;
  *t = APP_INITIALIZER;

  t->loop = ev_default_loop(EVFLAG_NOENV);

  if (mkdir_p(cfg.rundir, 0700) == -1 && errno != EEXIST) {
    fprintf(stderr, "mkdir: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if ((mkfifo(cfg.fifopath, 0600) == -1 && errno != EEXIST) ||
      (t->fifo_fd = open(cfg.fifopath, O_RDONLY|O_NONBLOCK, 0)) == -1) {
    log_error("fifo: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  setenv("LFMFIFO", cfg.fifopath, 1);

  /* inotify should be available on fm startup */
  if (notify_init(t) == -1) {
    log_error("inotify: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  t->ui.messages = NULL; /* needed to keep errors on fm startup */

  async_init(t);

  loader_init(app->loop);
  fm_init(&t->fm);
  ui_init(&t->ui, &t->fm);

  ev_idle_init(&t->redraw_watcher, redraw_cb);
  t->redraw_watcher.data = t;
  ev_idle_start(t->loop, &t->redraw_watcher);

  ev_prepare_init(&t->prepare_watcher, prepare_cb);
  t->prepare_watcher.data = t;
  ev_prepare_start(t->loop, &t->prepare_watcher);

  ev_timer_init(&t->timer_watcher, timer_cb, TICK, TICK);
  t->timer_watcher.data = t;
  ev_timer_start(t->loop, &t->timer_watcher);

  ev_io_init(&t->input_watcher, stdin_cb, notcurses_inputready_fd(t->ui.nc), EV_READ);
  t->input_watcher.data = t;
  ev_io_start(t->loop, &t->input_watcher);

  signal(SIGINT, SIG_IGN);

  ev_signal_init(&t->sigwinch_watcher, sigwinch_cb, SIGWINCH);
  t->sigwinch_watcher.data = t;
  ev_signal_start(t->loop, &t->sigwinch_watcher);

  ev_signal_init(&t->sigterm_watcher, sigterm_cb, SIGTERM);
  t->sigterm_watcher.data = t;
  ev_signal_start(t->loop, &t->sigterm_watcher);

  ev_signal_init(&t->sighup_watcher, sighup_cb, SIGHUP);
  t->sighup_watcher.data = t;
  ev_signal_start(t->loop, &t->sighup_watcher);

  t->L = luaL_newstate();
  lua_init(t->L, t);

  log_info("initialized app");
}


void app_run(T *t)
{
  ev_run(t->loop, 0);
}


void app_quit(T *t)
{
  lua_run_hook(t->L, LFM_HOOK_EXITPRE);
  ev_break(t->loop, EVBREAK_ALL);
}


static ev_io *add_io_watcher(T *t, FILE* f, int ind)
{
  if (!f) {
    return NULL;
  }

  const int fd = fileno(f);
  if (fd < 0) {
    log_error("add_io_watcher: fileno was %d", fd);
    fclose(f);
    return NULL;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  struct stdout_watcher_data *data = malloc(sizeof *data);
  data->app = t;
  data->stream = f;
  data->ind = ind;

  ev_io *w = malloc(sizeof *w);
  ev_io_init(w, command_stdout_cb, fd, EV_READ);
  w->data = data;
  ev_io_start(t->loop, w);

  return w;
}


static void add_child_watcher(T *t, int pid, int cb_index, ev_io *stdout_watcher, ev_io *stderr_watcher)
{
  struct child_watcher_data *data = malloc(sizeof *data);
  data->cb_index = cb_index > 0 ? cb_index : 0;
  data->app = t;
  data->stderr_watcher = stderr_watcher;
  data->stdout_watcher = stdout_watcher;

  ev_child *w = malloc(sizeof *w);
  ev_child_init(w, child_cb, pid, 0);
  w->data = data;
  ev_child_start(t->loop, w);

  cvector_push_back(t->child_watchers, w);
}


// spawn a background program
int app_spawn(T *t, const char *prog, char *const *args,
    char **in, bool out, bool err, int out_cb_ind, int err_cb_ind, int cb_ind)
{
  FILE *fout, *ferr, *fin;
  ev_io *stderr_watcher = NULL;
  ev_io *stdout_watcher = NULL;

  // always pass out and err because popen2_arr_p doesnt close the fds
  int pid = popen2_arr_p(in ? &fin : NULL, &fout, &ferr, prog, args, NULL);

  if (pid == -1) {
    error("popen2_arr_p: %s", strerror(errno));  // not sure if set
    return -1;
  }

  if (out || out_cb_ind) {
    stdout_watcher = add_io_watcher(t, fout, out_cb_ind);
  }
  else {
    fclose(fout);
  }

  if (err || err_cb_ind) {
    stderr_watcher = add_io_watcher(t, ferr, err_cb_ind);
  } else {
    fclose(ferr);
  }

  if (in) {
    cvector_foreach(line, in) {
      fputs(line, fin);
      fputc('\n', fin);
    }
    fclose(fin);
  }

  add_child_watcher(t, pid, cb_ind, stdout_watcher, stderr_watcher);
  return pid;
}


// execute a foreground program
bool app_execute(T *t, const char *prog, char *const *args)
{
  int pid, status, rc;
  ui_suspend(&t->ui);
  kbblocking(true);
  if ((pid = fork()) < 0) {
    status = -1;
  } else if (pid == 0) {
    // child
    signal(SIGINT, SIG_DFL);
    execvp(prog, (char* const *) args);
    _exit(127); // execl error
  } else {
    // parent
    signal(SIGINT, SIG_IGN);
    do {
      rc = waitpid(pid, &status, 0);
    } while ((rc == -1) && (errno == EINTR));
  }
  kbblocking(false);
  ui_resume(&t->ui);
  signal(SIGINT, SIG_IGN);
  ui_redraw(&t->ui, REDRAW_FM);
  return status == 0;
}


void print(const char *format, ...)
{
  va_list args;
  va_start(args, format);

  if (!app) {
    struct message msg = {.error = false};
    vasprintf(&msg.text, format, args);
    cvector_push_back(messages, msg);
  } else {
    ui_vechom(&app->ui, format, args);
  }

  va_end(args);
}


void error(const char *format, ...)
{
  va_list args;
  va_start(args, format);

  if (!app) {
    struct message msg = {.error = true};
    vasprintf(&msg.text, format, args);
    cvector_push_back(messages, msg);
  } else {
    ui_verror(&app->ui, format, args);
  }

  va_end(args);
}


void app_read_fifo(T *t)
{
  char buf[512];

  ssize_t nbytes = read(t->fifo_fd, buf, sizeof buf);

  if (nbytes <= 0) {
    return;
  }

  if ((size_t) nbytes < sizeof buf) {
    buf[nbytes - 1] = 0;
    lua_eval(t->L, buf);
  } else {
    size_t cap = 2 * sizeof(buf) / sizeof(*buf);
    char *dyn = malloc(cap * sizeof *dyn);
    size_t len = nbytes;
    memcpy(dyn, buf, nbytes);
    while ((nbytes = read(t->fifo_fd, dyn + len, cap - len)) > 0) {
      len += nbytes;
      if (len == cap) {
        cap *= 2;
        dyn = realloc(dyn, cap * sizeof *dyn);
      }
    }
    dyn[len] = 0;
    lua_eval(t->L, dyn);
    free(dyn);
  }

  ev_idle_start(t->loop, &app->redraw_watcher);
}


void app_schedule(T *t, int ind, uint32_t delay)
{
  struct schedule_timer_data *data = malloc(sizeof *data);
  data->app = t;
  data->ind = ind;
  ev_timer *w = malloc(sizeof *w);
  ev_timer_init(w, schedule_timer_cb, 1.0 * delay / 1000, 0);
  w->data = data;
  ev_timer_start(t->loop, w);
  cvector_push_back(t->schedule_timers, w);
}


void app_deinit(T *t)
{
  cvector_ffree(t->child_watchers, destroy_child_watcher);
  cvector_ffree(t->schedule_timers, destroy_schedule_timer);
  notify_deinit();
  lua_deinit(t->L);
  ui_deinit(&t->ui);
  fm_deinit(&t->fm);
  loader_deinit();
  async_deinit();
  if (t->fifo_fd > 0) {
    close(t->fifo_fd);
  }
  remove(cfg.fifopath);
}
