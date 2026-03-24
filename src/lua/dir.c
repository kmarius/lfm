#include "dir.h"
#include "config.h"
#include "stc/cstr.h"

#include "private.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>

#define DIR_META "Lfm.Dir.Meta"
#define DIR_METHODS "Lfm.Dir.Methods"

static inline void pushdir(lua_State *L, Dir *dir);
static inline void pushdir_from_path(lua_State *L, zsview path);
static inline Dir *checkdir(lua_State *L, int idx) {
  return *(Dir **)luaL_checkudata(L, idx, DIR_META);
}

/* methods */

int l_dir_up(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  i32 ct = luaL_optint(L, 2, 1);
  dir_cursor_move(dir, ct, fm->height, cfg.scrolloff);
  return 0;
}

int l_dir_down(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  i32 ct = luaL_optint(L, 2, 1);
  dir_cursor_move(dir, -ct, fm->height, cfg.scrolloff);
  return 0;
}

int l_dir_select(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  zsview name = luaL_checkzsview(L, 2);
  dir_cursor_move_to(dir, name, fm->height, cfg.scrolloff);
  return 0;
}

/* metamethods */

int l_dir__index(lua_State *L) {
  const char *field = luaL_checkstring(L, 2);

  // check if the field is a method
  lua_getfield(L, LUA_REGISTRYINDEX, DIR_METHODS);
  lua_getfield(L, -1, field);
  if (!lua_isnil(L, -1))
    return 1;
  lua_pop(L, 2);

  Dir *dir = checkdir(L, 1);
  if (streq(field, "path")) {
    lua_pushzsview(L, dir_path(dir));
  } else if (streq(field, "name")) {
    lua_pushzsview(L, dir_name(dir));
  } else if (streq(field, "parent")) {
    if (dir_isroot(dir))
      return 0;
    zsview path = path_parent_zv(dir_path(dir));
    pushdir_from_path(L, path);
  } else if (streq(field, "size")) {
    // TODO: how do we want to show visible/hidden/filtered?
    lua_pushinteger(L, dir_length(dir));
  } else if (streq(field, "index")) {
    lua_pushinteger(L, dir->ind + 1);
  } else if (streq(field, "files")) {
    lua_createtable(L, dir_length(dir), 0);
    usize i = 1;
    c_foreach(it, Dir, dir) {
      lua_pushzsview(L, file_name(*it.ref));
      lua_rawseti(L, -2, i++);
    }
  } else if (streq(field, "current_file")) {
    File *file = dir_current_file(dir);
    if (!file)
      return 0;
    lua_pushzsview(L, file_name(file));
  } else {
    return luaL_error(L, "invalid field: %s", field);
  }
  return 1;
}

int l_dir__gc(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  dir->lua_ref_count--;
  return 0;
}

static const struct luaL_Reg dir_methods[] = {
    {"up",     l_dir_up    },
    {"down",   l_dir_down  },
    {"select", l_dir_select},
    {NULL,     NULL        }
};

static const struct luaL_Reg dir_metamethods[] = {
    {"__gc",    l_dir__gc   },
    {"__index", l_dir__index},
    {NULL,      NULL        }
};

static inline void pushdir(lua_State *L, Dir *dir) {
  struct Dir **ud = lua_newuserdata(L, sizeof *ud);
  *ud = dir;
  dir->lua_ref_count++;

  if (luaL_newmetatable(L, DIR_META)) {
    luaL_register(L, NULL, dir_metamethods);

    lua_pushliteral(L, DIR_METHODS);
    luaL_newlib(L, dir_methods);
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  lua_setmetatable(L, -2);
}

static inline void pushdir_from_path(lua_State *L, zsview path) {
  Dir *dir = loader_dir_from_path(&lfm->loader, path, true);
  pushdir(L, dir);
}

// registered in apilib.c
int l_get_dir(lua_State *L) {
  zsview path = luaL_checkzsview(L, 1);
  pushdir_from_path(L, path);
  return 1;
}
