#pragma once

#include "fm.h"
#include "lfm.h"
#include "ui.h"

#include <lauxlib.h>
#include <lua.h>

extern Lfm *lfm;
extern struct async_ctx *async;
extern Ui *ui;
extern Fm *fm;

// update fm state, call when the current directory is changed
static inline void update_preview(bool immediate) {
  ui_on_cursor_moved(ui, immediate);
}

#define luaL_optbool(L, i, d) lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

#define lua_quit(L, lfm)                                                       \
  (lfm_quit(lfm, luaL_optint(L, -1, 0)),                                       \
   luaL_error(L, "no actual error, just quiting"));

// Stores the element at position idx in the registry and returns the
// reference.
static inline int lua_register_callback(lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TFUNCTION);
  lua_pushvalue(L, idx);
  return luaL_ref(L, LUA_REGISTRYINDEX);
}

// set or replace the callback a at reference `*ref` with the stack value at
// `idx`. `false` removes the callback and sets `*ref` to 0.
static inline void lua_replace_callback(lua_State *L, i32 idx, i32 *ref) {
  if (*ref == 0) {
    // new callback
    if (lua_toboolean(L, idx)) {
      luaL_checktype(L, idx, LUA_TFUNCTION);
      lua_pushvalue(L, idx);
      *ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  } else {
    // replace/remove existing
    if (lua_toboolean(L, idx)) {
      luaL_checktype(L, idx, LUA_TFUNCTION);
      lua_pushvalue(L, idx);
      lua_rawseti(L, LUA_REGISTRYINDEX, *ref);
    } else {
      luaL_unref(L, LUA_REGISTRYINDEX, *ref);
      *ref = 0;
    }
  }
}
