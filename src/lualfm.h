#ifndef LFMLUA_H
#define LFMLUA_H

#include <lua.h>

#include "app.h"

/*
 * Initialize lua state, load libraries.
 */
void lua_init(lua_State *L, app_t *app);

/*
 * Loads a .lua file.
 */
bool lua_load_file(lua_State *L, app_t *app, const char *path);

/*
 * Handles key input, called by the event listener, calls the lua function
 * handle_key
 */
void lua_handle_key(lua_State *L, app_t *app, ncinput *in);

/*
 * Execute an expr, which is either a chunk of lua code or a registered command
 * (with arguments) as if typed in the command line.
 */
void lua_exec_expr(lua_State *L, app_t *app, const char *cmd);

/*
 * Run hooks. Hook need to be registered in core.lua.
 */
void lua_run_hook(lua_State *L, const char *hook);

#endif
