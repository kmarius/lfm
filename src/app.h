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

void app_restart_redraw_watcher(app_t *app);

/*
 * Set input timout. Key input will be ignored for the next LEN ms.
 * */
void timeout_set(int duration);

void app_init(app_t *app);

void app_run(app_t *app);

void app_quit(app_t *app);

void app_deinit(app_t *app);

void print(const char *format, ...);

void error(const char *format, ...);

#endif /* APP_H */
