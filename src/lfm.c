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
#include "keys.h"
#include "hashtab.h"
#include "hooks.h"
#include "input.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "lualfm.h"
#include "notify.h"
#include "popen_arr.h"
#include "util.h"

#define TICK 1  // in seconds

struct message {
  char *text;
  bool error;
};

// callbacks {{{

// child watchers {{{

struct stdout_watcher_data {
  Lfm *lfm;
  FILE *stream;
  int ref;
};

struct child_watcher_data {
  Lfm *lfm;
  int ref;
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
  if (data->ref) {
    lua_run_stdout_callback(data->lfm->L, data->ref, NULL);
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

  if (data->ref >= 0) {
    lua_run_child_callback(data->lfm->L, data->ref, w->rstatus);
  }

  if (data->stdout_watcher) {
    ev_io_stop(loop, data->stdout_watcher);
  }

  if (data->stderr_watcher) {
    ev_io_stop(loop, data->stderr_watcher);
  }

  cvector_swap_remove(data->lfm->child_watchers, w);
  destroy_child_watcher(w);
}

// }}}

// scheduling timers {{{

struct schedule_timer_data {
  Lfm *lfm;
  int ref;
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
  lua_run_callback(data->lfm->L, data->ref);
  cvector_swap_remove(data->lfm->schedule_timers, w);
  ev_idle_start(loop, &data->lfm->redraw_watcher);
  destroy_schedule_timer(w);
}

// }}}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
  (void) revents;
  /* Lfm *lfm = w->data; */
  ev_timer_stop(loop, w);
}


static void stdin_cb(EV_P_ ev_io *w, int revents)
{
  (void) revents;
  Lfm *lfm = w->data;
  ncinput in;

  while (notcurses_get_nblock(lfm->ui.nc, &in) != (uint32_t) -1) {
    if (in.id == 0) {
      break;
    }
    // to emulate legacy with the kitty protocol (once it works in notcurses)
    // if (in.evtype == NCTYPE_RELEASE) {
    //   continue;
    // }
    // if (in.id >= NCKEY_LSHIFT && in.id <= NCKEY_L5SHIFT) {
    //   continue;
    // }
    if (current_millis() <= lfm->input_timeout) {
      continue;
    }

    // log_debug("id: %d, shift: %d, ctrl: %d alt %d, type: %d, %s", in.id, in.shift, in.ctrl, in.alt, in.evtype, in.utf8);
    lfm_handle_key(lfm, ncinput_to_input(&in));
  }

  ev_idle_start(loop, &lfm->redraw_watcher);
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

    if (data->ref >= 0) {
      lua_run_stdout_callback(data->lfm->L, data->ref, line);
    } else {
      ui_echom(&data->lfm->ui, "%s", line);
    }
  }
  free(line);

  // this seems to prevent the callback being immediately called again by libev
  if (errno == EAGAIN) {
    clearerr(data->stream);
  }

  ev_idle_start(loop, &data->lfm->redraw_watcher);
}


// To run command line cmds after loop starts. I think it is called back before
// every other cb.
static void prepare_cb(EV_P_ ev_prepare *w, int revents)
{
  (void) revents;
  Lfm *lfm = w->data;

  if (cfg.commands) {
    cvector_foreach(const char *cmd, cfg.commands) {
      lua_eval(lfm->L, cmd);
    }
    cvector_free(cfg.commands);
    cfg.commands = NULL;
  }

  if (lfm->messages) {
    cvector_foreach_ptr(struct message *m, lfm->messages) {
      if (m->error) {
        lfm_error(lfm, "%s", m->text);
      } else {
        lfm_print(lfm, "%s", m->text);
      }
      free(m->text);
    }
    cvector_free(lfm->messages);
    lfm->messages = NULL;
  }

  lfm_run_hook(lfm, LFM_HOOK_ENTER);
  ev_prepare_stop(loop, w);
}


static void sigwinch_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  Lfm *lfm = w->data;
  ui_clear(&lfm->ui);
  lfm_run_hook(lfm, LFM_HOOK_RESIZED);
  ev_idle_start(loop, &lfm->redraw_watcher);
}


static void sigterm_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  (void) loop;
  lfm_quit(w->data);
}


static void sighup_cb(EV_P_ ev_signal *w, int revents)
{
  (void) revents;
  (void) loop;
  lfm_quit(w->data);
}


static void redraw_cb(EV_P_ ev_idle *w, int revents)
{
  (void) revents;
  Lfm *lfm = w->data;
  ui_draw(&lfm->ui);
  ev_idle_stop(loop, w);
}

/* callbacks }}} */

void lfm_init(Lfm *lfm)
{
  memset(lfm, 0, sizeof *lfm);

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
      (lfm->fifo_fd = open(cfg.fifopath, O_RDONLY|O_NONBLOCK, 0)) == -1) {
    log_error("fifo: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  setenv("LFMFIFO", cfg.fifopath, 1);

  if (mkdir_p(cfg.cachedir, 0700) == -1 && errno != EEXIST) {
    log_error("fifo: %s", strerror(errno));
  }

  /* notify should be available on fm startup */
  if (!notify_init(&lfm->notify, lfm)) {
    log_error("inotify: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  loader_init(&lfm->loader, lfm);

  async_init(&lfm->async, lfm);

  fm_init(&lfm->fm, lfm);
  ui_init(&lfm->ui, lfm);

  input_init(lfm);

  ev_idle_init(&lfm->redraw_watcher, redraw_cb);
  lfm->redraw_watcher.data = lfm;
  ev_idle_start(lfm->loop, &lfm->redraw_watcher);

  ev_prepare_init(&lfm->prepare_watcher, prepare_cb);
  lfm->prepare_watcher.data = lfm;
  ev_prepare_start(lfm->loop, &lfm->prepare_watcher);

  ev_timer_init(&lfm->timer_watcher, timer_cb, TICK, TICK);
  lfm->timer_watcher.data = lfm;
  ev_timer_start(lfm->loop, &lfm->timer_watcher);

  ev_io_init(&lfm->input_watcher, stdin_cb, notcurses_inputready_fd(lfm->ui.nc), EV_READ);
  lfm->input_watcher.data = lfm;
  ev_io_start(lfm->loop, &lfm->input_watcher);

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
  lua_init(lfm->L, lfm);
  // can't run these hooks in the loader before initialization
  ht_foreach(Dir *dir, lfm->loader.dir_cache) {
    lfm_run_hook1(lfm, LFM_HOOK_DIRLOADED, dir->path);
  }

  log_info("initialized lfm");
}


void lfm_run(Lfm *lfm)
{
  ev_run(lfm->loop, 0);
}


void lfm_quit(Lfm *lfm)
{
  lfm_run_hook(lfm, LFM_HOOK_EXITPRE);
  ev_break(lfm->loop, EVBREAK_ALL);
}


static ev_io *add_io_watcher(Lfm *lfm, FILE* f, int ref)
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
  data->lfm = lfm;
  data->stream = f;
  data->ref = ref;

  ev_io *w = malloc(sizeof *w);
  ev_io_init(w, command_stdout_cb, fd, EV_READ);
  w->data = data;
  ev_io_start(lfm->loop, w);

  return w;
}


static void add_child_watcher(Lfm *lfm, int pid, int ref, ev_io *stdout_watcher, ev_io *stderr_watcher)
{
  struct child_watcher_data *data = malloc(sizeof *data);
  data->ref = ref;
  data->lfm = lfm;
  data->stdout_watcher = stdout_watcher;
  data->stderr_watcher = stderr_watcher;

  ev_child *w = malloc(sizeof *w);
  ev_child_init(w, child_cb, pid, 0);
  w->data = data;
  ev_child_start(lfm->loop, w);

  cvector_push_back(lfm->child_watchers, w);
}


// spawn a background program
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args,
    char **in, bool out, bool err, int out_cb_ref, int err_cb_ref, int cb_ref)
{
  FILE *fout, *ferr, *fin;
  ev_io *stderr_watcher = NULL;
  ev_io *stdout_watcher = NULL;

  // always pass out and err because popen2_arr_p doesnt close the fds
  int pid = popen2_arr_p(in ? &fin : NULL, &fout, &ferr, prog, args, NULL);

  if (pid == -1) {
    lfm_error(lfm, "popen2_arr_p: %s", strerror(errno));  // not sure if set
    return -1;
  }

  if (out || out_cb_ref >= 0) {
    stdout_watcher = add_io_watcher(lfm, fout, out_cb_ref);
  } else {
    fclose(fout);
  }

  if (err || err_cb_ref >= 0) {
    stderr_watcher = add_io_watcher(lfm, ferr, err_cb_ref);
  } else {
    fclose(ferr);
  }

  if (in) {
    cvector_foreach(char *line, in) {
      fputs(line, fin);
      fputc('\n', fin);
    }
    fclose(fin);
  }

  add_child_watcher(lfm, pid, cb_ref, stdout_watcher, stderr_watcher);
  return pid;
}


// execute a foreground program
bool lfm_execute(Lfm *lfm, const char *prog, char *const *args)
{
  int pid, status, rc;
  ev_io_stop(lfm->loop, &lfm->input_watcher);
  ui_suspend(&lfm->ui);
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
  ui_resume(&lfm->ui);

  ev_io_init(&lfm->input_watcher, stdin_cb, notcurses_inputready_fd(lfm->ui.nc), EV_READ);
  lfm->input_watcher.data = lfm;
  ev_io_start(lfm->loop, &lfm->input_watcher);

  signal(SIGINT, SIG_IGN);
  ui_redraw(&lfm->ui, REDRAW_FM);
  return status == 0;
}


void lfm_print(Lfm *lfm ,const char *format, ...)
{
  va_list args;
  va_start(args, format);

  if (!lfm->ui.lfm) {
    struct message msg = {.error = false};
    vasprintf(&msg.text, format, args);
    cvector_push_back(lfm->messages, msg);
  } else {
    ui_vechom(&lfm->ui, format, args);
  }

  va_end(args);
}


void lfm_error(Lfm *lfm, const char *format, ...)
{
  va_list args;
  va_start(args, format);

  if (!lfm->ui.lfm) {
    struct message msg = {.error = true};
    vasprintf(&msg.text, format, args);
    cvector_push_back(lfm->messages, msg);
  } else {
    ui_verror(&lfm->ui, format, args);
  }

  va_end(args);
}


void lfm_read_fifo(Lfm *lfm)
{
  char buf[512];

  ssize_t nbytes = read(lfm->fifo_fd, buf, sizeof buf);

  if (nbytes <= 0) {
    return;
  }

  if ((size_t) nbytes < sizeof buf) {
    buf[nbytes - 1] = 0;
    lua_eval(lfm->L, buf);
  } else {
    size_t cap = 2 * sizeof(buf) / sizeof(*buf);
    char *dyn = malloc(cap * sizeof *dyn);
    size_t len = nbytes;
    memcpy(dyn, buf, nbytes);
    while ((nbytes = read(lfm->fifo_fd, dyn + len, cap - len)) > 0) {
      len += nbytes;
      if (len == cap) {
        cap *= 2;
        dyn = realloc(dyn, cap * sizeof *dyn);
      }
    }
    dyn[len] = 0;
    lua_eval(lfm->L, dyn);
    free(dyn);
  }

  ev_idle_start(lfm->loop, &lfm->redraw_watcher);
}


void lfm_schedule(Lfm *lfm, int ref, uint32_t delay)
{
  struct schedule_timer_data *data = malloc(sizeof *data);
  data->lfm = lfm;
  data->ref = ref;
  ev_timer *w = malloc(sizeof *w);
  ev_timer_init(w, schedule_timer_cb, 1.0 * delay / 1000, 0);
  w->data = data;
  ev_timer_start(lfm->loop, w);
  cvector_push_back(lfm->schedule_timers, w);
}


void lfm_deinit(Lfm *lfm)
{
  cvector_ffree(lfm->child_watchers, destroy_child_watcher);
  cvector_ffree(lfm->schedule_timers, destroy_schedule_timer);
  notify_deinit(&lfm->notify);
  input_deinit(lfm);
  lua_deinit(lfm->L);
  ui_deinit(&lfm->ui);
  fm_deinit(&lfm->fm);
  loader_deinit(&lfm->loader);
  async_deinit(&lfm->async);
  if (lfm->fifo_fd > 0) {
    close(lfm->fifo_fd);
  }
  remove(cfg.fifopath);
}
