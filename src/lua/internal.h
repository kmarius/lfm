#pragma once

#include <lua.h>

#include "../lfm.h"
#include "../fm.h"
#include "../ui.h"

extern Lfm *lfm;
extern Ui *ui;
extern Fm *fm;

#define luaL_optbool(L, i, d) \
  lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

// stores the function on top of the stack in the registry and returns the
// reference index
static inline int lua_set_callback(lua_State *L)
{
  return luaL_ref(L, LUA_REGISTRYINDEX);
}

static inline bool lua_get_callback(lua_State *L, int ref, bool unref)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (unref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  return lua_type(L, -1) == LUA_TFUNCTION;
}
