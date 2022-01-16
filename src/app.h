#pragma once

#include <ev.h>
#include <lua.h>

#include "fm.h"
#include "tpool.h"
#include "ui.h"

typedef struct {
	Ui ui;
	Fm fm;
	lua_State *L;
	struct ev_loop *loop;
} App;

/*
 * Set input timout. Key input will be ignored for the next `duration` ms.
 */
void app_timeout_set(App *app, uint16_t duration);

/*
 * Initialize ui, fm and the lua_State.
 */
void app_init(App *app);

/*
 * Start the main event loop.
 */
void app_run(App *app);

/*
 * Stop the main event loop.
 */
void app_quit(App *app);

/*
 * Free all recources i.e. ui, fm and the lua_State.
 */
void app_deinit(App *app);

/*
 * Execute a command in the background and redirect its output/error to the ui
 * if `out` or `err` are set to `true`.
 */
bool app_execute(App *app, const char *prog, char *const *args, bool fork, bool out, bool err, int key);

/*
 * Print a message in the UI. `printf` formatting applies.
 */
void print(const char *format, ...);

/*
 * Print an error in the UI. `printf` formatting applies.
 */
void error(const char *format, ...);
