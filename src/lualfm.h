#pragma once

#include <lua.h>

#include "app.h"
#include "keys.h"

/*
 * Initialize lua state, load libraries.
 */
void lua_init(lua_State *L, App *app);

void lua_deinit(lua_State *L);

/*
 * Loads a .lua file.
 */
bool lua_load_file(lua_State *L, const char *path);

/*
 * Handles key input, called by the event listener, calls the lua function
 * handle_key
 */
void lua_handle_key(lua_State *L, input_t u);

/*
 * Execute an expr, which is either a chunk of lua code or a registered command
 * (with arguments) as if typed in the command line.
 */
void lua_eval(lua_State *L, const char *cmd);

/*
 * Run hooks. Hook need to be registered in core.lua.
 */
void lua_run_hook(lua_State *L, const char *hook);

/*
 * Run callback for finished child.
 */
void lua_run_callback(lua_State *L, int key, int rstatus);
