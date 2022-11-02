#pragma once

#include <ev.h>
#include <lua.h>
#include <stdint.h>

#include "async.h"
#include "fm.h"
#include "loader.h"
#include "notify.h"
#include "ui.h"

typedef struct lfm_s {
  Ui ui;
  Fm fm;
  lua_State *L;
  struct ev_loop *loop;
  Notify notify;
  Loader loader;
  Async async;

  int fifo_fd;

  ev_io input_watcher;
  uint64_t input_timeout;
  struct {
    struct trie_s *normal;  // normal mode mappings
    struct trie_s *cmd;     // command mode mappings
    struct trie_s *cur;     // pointer to the current leaf in either of the tries
    input_t *seq;           // current key sequence
    int count;
    bool accept_count;
  } maps;

  struct message_s *messages;

  ev_idle redraw_watcher;
  ev_prepare prepare_watcher;
  ev_signal sigwinch_watcher;
  ev_signal sigterm_watcher;
  ev_signal sighup_watcher;
  ev_timer timer_watcher;

  cvector_vector_type(ev_timer *) schedule_timers;
  cvector_vector_type(ev_child *) child_watchers; /* to run callbacks when processes finish */
} Lfm;

// Initialize ui, fm and the lua_State.
void lfm_init(Lfm *lfm);

// Start the main event loop.
void lfm_run(Lfm *lfm);

// Stop the event loop.
void lfm_quit(Lfm *lfm);

// Free all recources i.e. ui, fm and the lua_State.
void lfm_deinit(Lfm *lfm);

// Try reading from the $LFMFIFO
void lfm_read_fifo(Lfm *lfm);

// Spawn a background command. execvp semantics hold for `prog`, `args`.
// A cvector of strings can be passed by `in` and will be send to the commands
// standard input. If `out` or `err` are true, output/errors will be shown in
// the ui. If `out_cb_ref` or `err_cb_ref` are set, the respective callbacks
// are called with each line of output/error. `cb_ref` will be called with the
// return code once the command finishes.
int lfm_spawn(Lfm *lfm, const char *prog, char *const *args,
    char **in, bool out, bool err, int out_cb_ref, int err_cb_ref, int cb_ref);

// Execute a foreground program. Uses execvp semantics.
bool lfm_execute(Lfm *lfm, const char *prog, char *const *args);

// Schedule callback of the function given by `ref` in `delay` milliseconds.
void lfm_schedule(Lfm *lfm, int ref, uint32_t delay);

// Print a message in the UI. `printf` formatting applies.
void lfm_print(Lfm *lfm, const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void lfm_error(Lfm *lfm, const char *format, ...);
