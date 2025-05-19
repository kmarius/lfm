#pragma once

#include "../fm.h"
#include "../lfm.h"
#include "../ui.h"

#include <lauxlib.h>
#include <lua.h>

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
