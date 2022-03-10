#pragma once

#include <ev.h>
#include <lua.h>

#include "fm.h"
#include "ui.h"

typedef struct App {
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

	cvector_vector_type(ev_child *) child_watchers; /* to run callbacks when processes finish */
} App;

// Set input timout. Key input will be ignored for the next `duration` ms.
static inline void app_timeout_set(App *app, uint16_t duration)
{
	app->input_timeout = current_millis() + duration;
}

// Try reading from the $LFMFIFO
void app_read_fifo(App *app);

// Initialize ui, fm and the lua_State.
void app_init(App *app);

// Start the main event loop.
void app_run(App *app);

// Stop the main event loop.
void app_quit(App *app);

// Free all recources i.e. ui, fm and the lua_State.
void app_deinit(App *app);

// Execute a command in the background and redirect its output/error to the ui
// if `out` or `err` are set to `true`.
bool app_execute(App *app, const char *prog, char *const *args, bool fork, bool out, bool err, int key);

// Print a message in the UI. `printf` formatting applies.
void print(const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void error(const char *format, ...);
