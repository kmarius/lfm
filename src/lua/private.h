#pragma once

#include <lauxlib.h>
#include <lua.h>

#include "../fm.h"
#include "../lfm.h"
#include "../ui.h"

extern Lfm *lfm;
extern Ui *ui;
extern Fm *fm;

#define luaL_optbool(L, i, d) lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

#define lua_quit(L, lfm)                                                       \
  (lfm_quit(lfm, luaL_optint(L, -1, 0)),                                       \
   luaL_error(L, "no actual error, just quiting"));

// Stores the element at the top of the stack in the registry and returns the
// reference index.
static inline int lua_set_callback(lua_State *L) {
  luaL_checktype(L, -1, LUA_TFUNCTION);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  assert(ref > 0);
  return ref;
}

// Gets the previously stored (via lua_set_callback) element with reference ref
// from the registry and leaves it at the top of the stack.
static inline void lua_get_callback(lua_State *L, int ref, bool unref) {
  assert(ref > 0);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [elem]
  if (unref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  assert(!lua_isnoneornil(L, -1));
}
