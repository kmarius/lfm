#include "../config.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdint.h>

#define PATHS_META "Lfm.Paths.Meta"

static int l_paths_newindex(lua_State *L) {
  return luaL_error(L, "can not modify lfm.paths");
}

static const struct luaL_Reg paths_mt[] = {{"__newindex", l_paths_newindex},
                                           {NULL, NULL}};

int luaopen_paths(lua_State *L) {
  lua_newtable(L);

  lua_pushstring(L, cfg.fifopath);
  lua_setfield(L, -2, "fifo");

  lua_pushstring(L, cfg.logpath);
  lua_setfield(L, -2, "log");

  const char *path = cfg.user_configpath ? cfg.user_configpath : cfg.configpath;
  lua_pushstring(L, path);
  lua_setfield(L, -2, "config");

  lua_pushstring(L, cfg.configdir);
  lua_setfield(L, -2, "config_dir");

  lua_pushstring(L, cfg.luadir);
  lua_setfield(L, -2, "lua_dir");

  lua_pushstring(L, cfg.datadir);
  lua_setfield(L, -2, "data_dir");

  lua_pushstring(L, cfg.statedir);
  lua_setfield(L, -2, "state_dir");

  lua_pushstring(L, cfg.rundir);
  lua_setfield(L, -2, "runtime_dir");

  luaL_newmetatable(L, PATHS_META);
  luaL_register(L, NULL, paths_mt);
  lua_setmetatable(L, -2);

  return 1;
}
