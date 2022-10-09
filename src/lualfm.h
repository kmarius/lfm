#pragma once

#include <lua.h>

#include "keys.h"
#include "lfm.h"

// Initialize lua state, load libraries.
void lua_init(lua_State *L, Lfm *lfm);

void lua_deinit(lua_State *L);

// Handles key input, called by the event listener, calls the lua function
// handle_key
void lua_handle_key(lua_State *L, input_t in);

// Evaluate an expr, which is either a chunk of lua code or a registered command
// (with arguments) as if typed in the command line.
void lua_eval(lua_State *L, const char *expr);

// Run hooks. Hook need to be registered in core.lua.
void lua_run_hook(lua_State *L, const char *hook);

void lua_run_hook1(lua_State *L, const char *hook, const char* arg1);

//  Run callback for finished child.
void lua_run_child_callback(lua_State *L, int ref, int rstatus);

void lua_run_callback(lua_State *L, int ref);

// `line==NULL` removes callback from the registry.
void lua_run_stdout_callback(lua_State *L, int ref, const char *line);
