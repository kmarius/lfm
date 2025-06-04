#pragma once

/*
 * Functions to interface with lua from the C side such as running hooks,
 * callbacks etc.
 */

#include "../stc/zsview.h"

#include <lua.h>

#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

struct Lfm;
struct Preview;
struct Async;

// Initialize lua state, load libraries.
void lfm_lua_init(struct Lfm *lfm);

void lfm_lua_init_thread(lua_State *L);

void lfm_lua_deinit(struct Lfm *lfm);

int llua_pcall(lua_State *L, int nargs, int nresults);

// Evaluate an expr, which is either a chunk of lua code or a registered command
// (with arguments) as if typed in the command line.
void llua_evaln(lua_State *L, const char *expr, int len);

static inline void llua_eval(lua_State *L, const char *expr) {
  llua_evaln(L, expr, strlen(expr));
}

static inline void llua_eval_zsview(lua_State *L, zsview expr) {
  llua_evaln(L, expr.str, expr.size);
}

//  Run callback for finished child.
void llua_run_child_callback(lua_State *L, int ref, int rstatus);

void llua_run_callback(lua_State *L, int ref);

void llua_call_ref(lua_State *L, int ref);

void llua_call_ref1(lua_State *L, int ref, zsview line);

// `line==NULL` removes callback from the registry.
void llua_run_stdout_callback(lua_State *L, int ref, const char *line,
                              ssize_t len);

// Call a function from reference, passing an optional count if it is positive
void llua_call_from_ref(lua_State *L, int ref, int count);

bool llua_filter(lua_State *L, int ref, zsview name);
