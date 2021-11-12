#ifndef APP_H
#define APP_H

#include <ev.h>
#include <lua.h>

#include "fm.h"
#include "tpool.h"
#include "ui.h"

typedef struct app_t {
	ui_t ui;
	fm_t fm;
	lua_State *L;
	struct ev_loop *loop;
} app_t;

/*
 * Set input timout. Key input will be ignored for the next `duration` ms.
 */
void timeout_set(int duration);

/*
 * Initialize ui, fm and the lua_State.
 */
void app_init(app_t *app);

/*
 * Start the main event loop.
 */
void app_run(app_t *app);

/*
 * Stop the main event loop.
 */
void app_quit(app_t *app);

/*
 * Free all recources i.e. ui, fm and the lua_State.
 */
void app_deinit(app_t *app);

/*
 * Print a message in the UI. `printf` formatting applies.
 */
void print(const char *format, ...);

/*
 * Print an error in the UI. `printf` formatting applies.
 */
void error(const char *format, ...);

#endif /* APP_H */
