#pragma once

#include <ev.h>
#include <lua.h>
#include <stdint.h>

#include "fm.h"
#include "ui.h"

typedef struct app {
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

bool app_spawn(App *app, const char *prog, char *const *args,
		bool out, bool err, int out_cb_ind, int err_cb_ind, int cb_ind);

bool app_execute(App *app, const char *prog, char *const *args);

void app_schedule(App *app, int schedule_ind, uint32_t delay);

// Print a message in the UI. `printf` formatting applies.
void print(const char *format, ...);

// Print an error in the UI. `printf` formatting applies.
void error(const char *format, ...);
