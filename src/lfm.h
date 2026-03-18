#pragma once

#include "async/async.h"
#include "fm.h"
#include "hooks.h"
#include "loader.h"
#include "mode.h"
#include "notify.h"
#include "types/vec_int.h"
#include "types/vec_zsview.h"
#include "ui.h"

#include "stc/types.h"

#include <ev.h>
#include <lua.h>

#include <stdint.h>

declare_vec(vec_hook_change, struct hook_change);
declare_dlist(list_timer, struct sched_timer);
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

  lua_State *L;

  hmap_modes modes;
  struct mode *current_mode;

  ev_prepare prepare_watcher;
#ifndef NDEBUG
  ev_check check_watcher;
#endif
  ev_signal sigint_watcher;
  ev_signal sigtstp_watcher;
  ev_signal sigwinch_watcher;
  ev_signal sigterm_watcher;
  ev_signal sighup_watcher;
  ev_signal sigpipe_watcher;

  list_timer schedule_timers;

  // We can not make changes to hooks while in a hook callback.
  // Hence, all changes are collected and processed afterwards.
  vec_int hook_refs[LFM_NUM_HOOKS];
  vec_hook_change hook_changes;
  i32 hook_callback_depth; // a callback could cause another hook to run

  vec_message messages;

  struct lfm_opts opts;

  int ret; /* set in lfm_quit and returned by lfm_run */
} Lfm;

// Initialize lfm and all its components.
void lfm_init(Lfm *lfm, struct lfm_opts *opts);

// Deinitialize lfm.
void lfm_deinit(Lfm *lfm);

// Start the main event loop.
i32 lfm_run(Lfm *lfm);

// Stop the event loop.
void lfm_quit(Lfm *lfm, i32 ret);

// call this on resize
void lfm_on_resize(Lfm *lfm);

// Schedule callback of the function given by `ref` in `delay` milliseconds.
void lfm_schedule(Lfm *lfm, i32 ref, u32 delay);

// Print a message in the UI. `printf` formatting applies.
void lfm_printf(Lfm *lfm, const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void lfm_errorf(Lfm *lfm, const char *format, ...);
