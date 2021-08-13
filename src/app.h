#ifndef APP_H
#define APP_H

#include <ev.h>
#include <lua.h>

#include "nav.h"
#include "tpool.h"
#include "ui.h"

typedef struct App {
	ui_t ui;
	nav_t nav;
	lua_State *L;
	struct ev_loop *loop;
} app_t;

/*
 * Set input timout. Key input will be ignored for the next LEN ms.
 * */
void app_timeout(int duration);

void app_init(app_t *app);

void run(app_t *app);

void app_quit(app_t *app);

void app_destroy(app_t *app);

void print(const char *format, ...);

void error(const char *format, ...);

#endif
