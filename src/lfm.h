#pragma once

#include "async.h"
#include "containers.h"
#include "fm.h"
#include "hooks.h"
#include "loader.h"
#include "mode.h"
#include "notify.h"
#include "ui.h"

#include "stc/types.h"

#include <ev.h>
#include <lua.h>

#include <stdint.h>

declare_dlist(list_timer, struct sched_timer);
declare_dlist(list_child, struct child_watcher);
struct vec_str; // defined in config.h

typedef struct Lfm {
  Ui ui;
  Fm fm;
  Notify notify;
  Loader loader;
  Async async;
  struct ev_loop *loop;

  lua_State *L;
  int lua_stack_size; // for debugging purposes to ensure we are not leaking
                      // stack elements

  hmap_modes modes;
  struct mode *current_mode;

  ev_prepare prepare_watcher;
  ev_timer timer_watcher;
  ev_signal sigint_watcher;
  ev_signal sigtstp_watcher;
  ev_signal sigwinch_watcher;
  ev_signal sigterm_watcher;
  ev_signal sighup_watcher;
  ev_signal sigpipe_watcher;
  ev_io fifo_watcher;

  list_timer schedule_timers;
  list_child child_watchers;

  vec_int hook_refs[LFM_NUM_HOOKS];

  vec_message messages;

  int fifo_fd;
  FILE *log_fp;
  int ret; /* set in lfm_quit and returned in main.c */
} Lfm;

// Initialize lfm and all its components.
void lfm_init(Lfm *lfm, FILE *log);

// Deinitialize lfm.
void lfm_deinit(Lfm *lfm);

// Start the main event loop.
int lfm_run(Lfm *lfm);

// Stop the event loop.
void lfm_quit(Lfm *lfm, int ret);

// Spawn a background command. execvp semantics hold for `prog`, `args`.
// A vector of strings can be passed by `stdin_lines` and will be send to the
// commands standard input. If `out` or `err` are true, output/errors will be
// shown in the ui. If `stdout_ref` or `stderr_ref` are set (>0), the
// respective callbacks are called with each line of output/error and nothing
// will be printed on the ui. `exit_ref` will be called with the return code
// once the command finishes.
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args, env_list *env,
              const vec_bytes *stdin_lines, int *stdin_fd, bool capture_stdout,
              bool capture_stderr, int stdout_ref, int stderr_ref,
              int exit_ref);

// Execute a foreground program. Uses execvp semantics. If stdout is passed,
// lines from stdout are captured in the vector. Returns the exit status of the
// process, or -1 if fork() fails.
int lfm_execute(Lfm *lfm, const char *prog, char *const *args, env_list *env,
                vec_bytes *stdin_lines, vec_bytes *stdout_lines,
                vec_bytes *stderr_lines);

// Schedule callback of the function given by `ref` in `delay` milliseconds.
void lfm_schedule(Lfm *lfm, int ref, uint32_t delay);

// Print a message in the UI. `printf` formatting applies.
void lfm_print(Lfm *lfm, const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void lfm_error(Lfm *lfm, const char *format, ...);
