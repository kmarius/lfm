#include "lfm.h"

#include "async/async.h"
#include "cleanup.h"
#include "config.h"
#include "defs.h"
#include "fifo.h"
#include "getpwd.h"
#include "hooks.h"
#include "input.h"
#include "loader.h"
#include "log.h"
#include "loop.h"
#include "lua/lfmlua.h"
#include "mode.h"
#include "notify.h"
#include "profiling.h"
#include "stc/common.h"
#include "stc/cstr.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>

struct ev_loop *event_loop = NULL;
static Lfm *instance = NULL;

struct sched_timer {
  ev_timer watcher;
  i32 ref; // lua ref
  u32 id;  // timer id, key in lfm->schedule_timers
};

#define i_declared
#define i_type timers, u32, struct sched_timer *
#define i_valraw struct sched_timer
#define i_valtoraw(p) (**(p))
#define i_valfrom heapify
#define i_valdrop(p) xfree(*(p))
#define i_no_clone
#include "stc/hmap.h"

Lfm *lfm_instance(void) {
  return instance;
}

static void schedule_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  struct sched_timer *timer = (struct sched_timer *)w;
  Lfm *lfm = w->data;
  ev_timer_stop(EV_A_ w);
  lfm_lua_cb(lfm->L, timer->ref);
  timers_erase(&lfm->schedule_timers, timer->id);
}

// To run command line cmds after loop starts. I think it is called back before
// every other cb.
static void prepare_cb(EV_P_ ev_prepare *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;

  vec_message messages = vec_message_move(&lfm->messages);
  c_foreach(it, vec_message, messages) {
    // takes ownership of the messages
    ui_display_message(&lfm->ui, *it.ref);
  }
  messages.size = 0;
  vec_message_drop(&messages);

  vec_zsview commands = vec_zsview_move(&lfm->opts.commands);
  c_foreach(it, vec_zsview, commands) {
    llua_eval_zsview(lfm->L, *it.ref);
  }
  vec_zsview_drop(&commands);

  lfm_run_hook(lfm, LFM_HOOK_ENTER);
  ev_prepare_stop(EV_A_ w);
}

#ifndef NDEBUG
static void check_cb(EV_P_ ev_check *w, i32 revents) {
  (void)revents;
  (void)w;
  static i32 count = 0;
  log_trace("ev_loop iteration % 5d", count++);
}
#endif

static void sigtstp_cb(EV_P_ ev_signal *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGTSTP");
  ev_signal_stop(loop, w);
  ui_suspend(&lfm->ui);
  raise(SIGTSTP);
  ui_resume(&lfm->ui);
  ui_redraw(&lfm->ui, REDRAW_FULL);
  ev_signal_start(loop, w);
}

static void sigint_cb(EV_P_ ev_signal *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGINT");
  input_handle_key(lfm, CTRL('C'));
}

// unclear if this happens before/after resizecb is called by notcurses
static void sigwinch_cb(EV_P_ ev_signal *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  log_trace("received SIGWINCH");
  ui_clear(&lfm->ui);
}

static void sigterm_cb(EV_P_ ev_signal *w, i32 revents) {
  (void)revents;
  (void)loop;
  log_trace("received SIGTERM");
  lfm_quit(w->data, 0);
}

static void sighup_cb(EV_P_ ev_signal *w, i32 revents) {
  (void)revents;
  (void)loop;
  log_trace("received SIGHUP");
  lfm_quit(w->data, 0);
}

static void sigpipe_cb(EV_P_ ev_signal *w, i32 revents) {
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
  ev_prepare_start(event_loop, &lfm->prepare_watcher);

#ifndef NDEBUG
  ev_prepare_init(&lfm->check_watcher, check_cb);
  lfm->check_watcher.data = lfm;
  ev_check_start(event_loop, &lfm->check_watcher);
#endif

  // Catch some signals
  ev_signal_init(&lfm->sigint_watcher, sigint_cb, SIGINT);
  lfm->sigint_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sigint_watcher);

  ev_signal_init(&lfm->sigwinch_watcher, sigwinch_cb, SIGWINCH);
  lfm->sigwinch_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sigwinch_watcher);

  ev_signal_init(&lfm->sigterm_watcher, sigterm_cb, SIGTERM);
  lfm->sigterm_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sigterm_watcher);

  ev_signal_init(&lfm->sighup_watcher, sighup_cb, SIGHUP);
  lfm->sighup_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sighup_watcher);

  ev_signal_init(&lfm->sigpipe_watcher, sigpipe_cb, SIGPIPE);
  lfm->sigpipe_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sigpipe_watcher);

  ev_signal_init(&lfm->sigtstp_watcher, sigtstp_cb, SIGTSTP);
  lfm->sigtstp_watcher.data = lfm;
  ev_signal_start(event_loop, &lfm->sigtstp_watcher);
}

void lfm_init(Lfm *lfm, struct lfm_opts *opts) {
  memset(lfm, 0, sizeof *lfm);
  lfm->opts = *opts;

  setpwd(getenv("PWD"));

  event_loop = ev_default_loop(EVFLAG_NOENV);
  instance = lfm;

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
  lfm_lua_deinit(lfm);
  call_dtors();
  lfm_modes_deinit(lfm);
  timers_drop(&lfm->schedule_timers);
  notify_deinit(&lfm->notify);
  ui_deinit(&lfm->ui);
  fm_deinit(&lfm->fm);
  lfm_hooks_deinit(lfm);
  loader_deinit(&lfm->loader);
  async_deinit(&lfm->async);
  fifo_deinit();

  cstr_drop(&lfm->opts.startfile);
  cstr_drop(&lfm->opts.startpath);
}

i32 lfm_run(Lfm *lfm) {
  ev_run(event_loop, 0);
  return lfm->ret;
}

void lfm_quit(Lfm *lfm, i32 ret) {
  lfm_run_hook(lfm, LFM_HOOK_EXITPRE, ret);
  ev_break(event_loop, EVBREAK_ALL);
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

void lfm_printf(Lfm *lfm, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  struct message msg = {};
  cstr_vfmt(&msg.text, 0, fmt, args);

  if (!lfm->ui.running) {
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_display_message(&lfm->ui, msg);
  }

  va_end(args);
}

void lfm_errorf(Lfm *lfm, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  struct message msg = {
      .error = true,
  };
  cstr_vfmt(&msg.text, 0, fmt, args);

  if (!lfm->ui.running) {
    vec_message_push(&lfm->messages, msg);
  } else {
    ui_display_message(&lfm->ui, msg);
  }

  va_end(args);
}

i32 lfm_schedule(Lfm *lfm, i32 ref, u32 delay) {
  u32 id = lfm->timers_ct++;
  timers_result res = timers_emplace(&lfm->schedule_timers, id,
                                     (struct sched_timer){
                                         .ref = ref,
                                         .id = id,
                                     });
  struct sched_timer *data = res.ref->second;
  ev_timer_init(&data->watcher, schedule_timer_cb, 1.0 * delay / 1000, 0);
  data->watcher.data = lfm;
  ev_timer_start(event_loop, &data->watcher);
  return id;
}

void lfm_cancel(Lfm *lfm, u32 id) {
  timers_iter it = timers_find(&lfm->schedule_timers, id);
  if (it.ref) {
    ev_timer_stop(event_loop, &it.ref->second->watcher);
    timers_erase_at(&lfm->schedule_timers, it);
  }
}
