#include "dir.h"
#include "lfm.h"
#include "lfmlua.h"
#include "lua.h"
#include "util.h"

#include "private.h"

#define KEYFUNCS "Lfm.Keyfuncs"

void lfm_lua_store_keyfunc(Lfm *lfm, i32 idx, zsview path) {
  lua_State *L = lfm->L;
  lua_pushvalue(L, idx);                        // [func]
  lua_getfield(L, LUA_REGISTRYINDEX, KEYFUNCS); // [func, keyfuncs]
  if (unlikely(lua_isnil(L, -1))) {
    lua_pop(L, 1);                                // [func]
    lua_newtable(L);                              // [func, keyfuncs]
    lua_pushvalue(L, -1);                         // [func, keyfuncs, keyfuncs]
    lua_setfield(L, LUA_REGISTRYINDEX, KEYFUNCS); // [func, keyfuncs]
  }
  lua_insert(L, -2); // [keyfuncs, func]
  lua_setfield(L, -2, path.str);
  lua_pop(L, 1);
}

static inline void get_keyfunc(lua_State *L, zsview path) {
  lua_getfield(L, LUA_REGISTRYINDEX, KEYFUNCS);
  lua_getfield(L, -1, path.str);
  lua_insert(L, -2);
  lua_pop(L, 1);
}

int lfm_lua_apply_keyfunc(Lfm *lfm, Dir *dir, bool throw) {
  lua_State *L = lfm->L;

  int ret = 0;

  get_keyfunc(L, dir_path(dir)); // [func]
  if (lua_isnil(L, -1))
    goto err;

  c_foreach(it, vec_file, dir->files_all) {
    File *file = *it.ref;
    lua_pushvalue(L, -1);               // [func, func]
    lua_pushzsview(L, file_name(file)); // [func, func, path]

    if (unlikely(lfm_lua_pcall(L, 1, 1) != 0)) {
      if (throw)
        return lua_error(L);
      lfm_errorf(lfm, "%s", lua_tostring(L, -1));
      goto lua_err;
    }
    if (unlikely(lua_type(L, -1) != LUA_TNUMBER)) {
      if (throw)
        return luaL_error(L, "integer expected, got %s", lua_tostring(L, -1));
      lfm_errorf(lfm, "keyfunc: integer expected, got %s", lua_tostring(L, -1));
      goto lua_err;
    }
    file->key = lua_tointeger(L, -1);

    lua_pop(L, 1); // [func]
  }

ret:
  // [func]
  lua_pop(L, 1);
  return ret;

lua_err:
  // [func, val/err]
  lua_pop(L, 1);

err:
  ret = 1;
  goto ret;
}
