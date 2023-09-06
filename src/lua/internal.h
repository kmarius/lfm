#pragma once

#include <lua.h>

#include "../fm.h"
#include "../lfm.h"
#include "../ui.h"

extern Lfm *lfm;
extern Ui *ui;
extern Fm *fm;

#define luaL_optbool(L, i, d) lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

#define lua_quit(L, lfm) (lfm_quit(lfm), luaL_error(L, "quit"));

// Stores the element at the top of the stack in the registry and returns the
// reference index.
static inline int lua_set_callback(lua_State *L) {
  return luaL_ref(L, LUA_REGISTRYINDEX);
}

// Gets the previously stored (via lua_set_callback) element with reference ref
// from the registry and leaves it at the top of the stack.
// Returns `true` if the element is a function.
static inline bool lua_get_callback(lua_State *L, int ref, bool unref) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [elem]
  if (unref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  return lua_type(L, -1) == LUA_TFUNCTION;
}
