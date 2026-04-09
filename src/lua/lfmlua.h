#pragma once

/*
 * Functions to interface with lua from the C side such as running hooks,
 * callbacks etc.
 */

#include "defs.h"

#include <lauxlib.h>
#include <lua.h>
#include <stc/zsview.h>

#include <stdbool.h>
#include <string.h>

struct Lfm;
struct Dir;

// Initialize lua state, load libraries.
void lfm_lua_init(struct Lfm *lfm);

// Initialize a lua state for a thread with limited functionality
void lfm_lua_init_thread(lua_State *L);

void lfm_lua_deinit(struct Lfm *lfm);

// pcall with traceback
int lfm_lua_pcall(lua_State *L, int nargs, int nresults);

// Evaluate an expr, which is either a chunk of lua code or a registered command
// (with arguments) as if typed in the command line.
void lfm_lua_evaln(lua_State *L, const char *expr, int len);

static inline void lfm_lua_eval(lua_State *L, const char *expr) {
  lfm_lua_evaln(L, expr, strlen(expr));
}

static inline void lfm_lua_eval_zsview(lua_State *L, zsview expr) {
  lfm_lua_evaln(L, expr.str, expr.size);
}

// Run a callback, optionally unref
void lfm_lua_cb(lua_State *L, int ref, bool unref);

// Callback with one string argument
void lfm_lua_cb1(lua_State *L, int ref, zsview line);

// Callback with count argument, only passed if > 0
void lfm_lua_cb_with_count(lua_State *L, int ref, int count);

//  Run callback for finished child.
void lfm_lua_child_exit_cb(lua_State *L, int ref, int rstatus);

// `line==NULL` removes callback from the registry. Set len < 0 if not known.
void lfm_lua_child_stdout_cb(lua_State *L, int ref, const char *line,
                             isize len);

// Evaluate a filter predicate on a file name
bool lfm_lua_filter(lua_State *L, int ref, zsview name);

// Gets the previously stored (via lua_set_callback) element with reference ref
// from the registry and leaves it at the top of the stack.
static inline void lfm_lua_push_callback(lua_State *L, i32 ref, bool unref) {
  if (unlikely(L == NULL))
    return;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [elem]
  if (unref)
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}

void lfm_lua_store_keyfunc(struct Lfm *lfm, i32 idx, zsview path);
int lfm_lua_apply_keyfunc(struct Lfm *lfm, struct Dir *dir, bool throw);
