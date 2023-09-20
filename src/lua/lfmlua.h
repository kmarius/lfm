#pragma once

#include <lua.h>

struct lfm_s;

// Initialize lua state, load libraries.
void llua_init(lua_State *L, struct lfm_s *lfm);

void llua_deinit(lua_State *L);

// Evaluate an expr, which is either a chunk of lua code or a registered command
// (with arguments) as if typed in the command line.
void llua_eval(lua_State *L, const char *expr);

//  Run callback for finished child.
void llua_run_child_callback(lua_State *L, int ref, int rstatus);

void llua_run_callback(lua_State *L, int ref);

void llua_call_ref(lua_State *L, int ref);

void llua_call_ref1(lua_State *L, int ref, const char *line);

// `line==NULL` removes callback from the registry.
void llua_run_stdout_callback(lua_State *L, int ref, const char *line);

// Call a function from reference, passing an optional count if it is positive
void llua_call_from_ref(lua_State *L, int ref, int count);
