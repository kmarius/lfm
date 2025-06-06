#pragma once

#include "async/async.h"
#include "fm.h"
#include "hooks.h"
#include "loader.h"
#include "mode.h"
#include "notify.h"
#include "ui.h"
#include "vec_int.h"
#include "vec_zsview.h"

#include "stc/types.h"

#include <ev.h>
#include <lua.h>

#include <stdint.h>

declare_dlist(list_timer, struct sched_timer);
declare_dlist(list_child, struct child_watcher);
struct vec_str;
struct vec_env;
struct vec_bytes;

// we will free startpath/startfile and commands in lfm
struct lfm_opts {
  FILE *log;
  vec_zsview commands;        // lua commands to run after start
  const char *lastdir_path;   // output current pwd on exit
  const char *selection_path; // output selection on open
  cstr startpath;             // override pwd
  cstr startfile;             // move cursor to this file
  const char *config;         // override config path
};

typedef struct Lfm {
  Ui ui;
  Fm fm;
  Notify notify;
  Loader loader;
  Async async;
  struct ev_loop *loop;

  lua_State *L;

  hmap_modes modes;
  struct mode *current_mode;

  ev_prepare prepare_watcher;
  ev_signal sigint_watcher;
  ev_signal sigtstp_watcher;
  ev_signal sigwinch_watcher;
  ev_signal sigterm_watcher;
  ev_signal sighup_watcher;
  ev_signal sigpipe_watcher;

  list_timer schedule_timers;
  list_child child_watchers;

  vec_int hook_refs[LFM_NUM_HOOKS];

  vec_message messages;

  struct lfm_opts opts;

  int ret; /* set in lfm_quit and returned in main.c */
} Lfm;

// Initialize lfm and all its components.
void lfm_init(Lfm *lfm, struct lfm_opts *opts);

// Deinitialize lfm.
void lfm_deinit(Lfm *lfm);

// Start the main event loop.
int lfm_run(Lfm *lfm);

// Stop the event loop.
void lfm_quit(Lfm *lfm, int ret);

// call this on resize
void lfm_on_resize(Lfm *lfm);

// Spawn a background command. execvp semantics hold for `prog`, `args`.
// A vector of strings can be passed by `stdin_lines` and will be send to the
// commands standard input. If `out` or `err` are true, output/errors will be
// shown in the ui. If `stdout_ref` or `stderr_ref` are set (>0), the
// respective callbacks are called with each line of output/error and nothing
// will be printed on the ui. `exit_ref` will be called with the return code
// once the command finishes.
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args,
              struct vec_env *env, const struct vec_bytes *stdin_lines,
              int *stdin_fd, bool capture_stdout, bool capture_stderr,
              int stdout_ref, int stderr_ref, int exit_ref,
              zsview working_directory);

// Execute a foreground program. Uses execvp semantics. If stdout is passed,
// lines from stdout are captured in the vector. Returns the exit status of the
// process, or -1 if fork() fails.
int lfm_execute(Lfm *lfm, const char *prog, char *const *args,
                struct vec_env *env, struct vec_bytes *stdin_lines,
                struct vec_bytes *stdout_lines, struct vec_bytes *stderr_lines);

// Schedule callback of the function given by `ref` in `delay` milliseconds.
void lfm_schedule(Lfm *lfm, int ref, uint32_t delay);

// Print a message in the UI. `printf` formatting applies.
void lfm_print(Lfm *lfm, const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void lfm_error(Lfm *lfm, const char *format, ...);
