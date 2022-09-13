#pragma once

#include <ev.h>
#include <lua.h>
#include <stdint.h>

#include "fm.h"
#include "ui.h"

typedef struct lfm_s {
  Ui ui;
  Fm fm;
  lua_State *L;
  struct ev_loop *loop;

  int fifo_fd;

  uint64_t input_timeout;

  ev_io input_watcher;
  ev_idle redraw_watcher;
  ev_prepare prepare_watcher;
  ev_signal sigwinch_watcher;
  ev_signal sigterm_watcher;
  ev_signal sighup_watcher;
  ev_timer timer_watcher;

  cvector_vector_type(ev_timer *) schedule_timers;
  cvector_vector_type(ev_child *) child_watchers; /* to run callbacks when processes finish */
} Lfm;

// Set input timout. Key input will be ignored for the next `duration` ms.
static inline void lfm_timeout_set(Lfm *lfm, uint32_t duration)
{
  lfm->input_timeout = current_millis() + duration;
}

// Try reading from the $LFMFIFO
void lfm_read_fifo(Lfm *lfm);

// Initialize ui, fm and the lua_State.
void lfm_init(Lfm *lfm);

// Start the main event loop.
void lfm_run(Lfm *lfm);

// Stop the main event loop.
void lfm_quit(Lfm *lfm);

// Free all recources i.e. ui, fm and the lua_State.
void lfm_deinit(Lfm *lfm);

int lfm_spawn(Lfm *lfm, const char *prog, char *const *args,
    char **in, bool out, bool err, int out_cb_ref, int err_cb_ref, int cb_ref);

bool lfm_execute(Lfm *lfm, const char *prog, char *const *args);

void lfm_schedule(Lfm *lfm, int ref, uint32_t delay);

// Print a message in the UI. `printf` formatting applies.
void print(const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void error(const char *format, ...);
