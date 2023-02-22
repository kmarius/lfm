#include <lauxlib.h>

#include "internal.h"
#include "../log.h"
#include "lua.h"

#define LFM_LOG_META "log_mt"
#define STR(x) #x

static int l_log_trace(lua_State *L)
{
  log_trace("%s", luaL_checkstring(L, 1));
  return 0;
}

static int l_log_debug(lua_State *L)
{
  log_debug("%s", luaL_checkstring(L, 1));
  return 0;
}

static int l_log_info(lua_State *L)
{
  log_info("%s", luaL_checkstring(L, 1));
  return 0;
}

static int l_log_warn(lua_State *L)
{
  log_warn("%s", luaL_checkstring(L, 1));
  return 0;
}

static int l_log_error(lua_State *L)
{
  log_error("%s", luaL_checkstring(L, 1));
  return 0;
}

static int l_log_fatal(lua_State *L)
{
  log_fatal("%s", luaL_checkstring(L, 1));
  return 0;
}

static const struct luaL_Reg log_lib[] = {
  {"trace", l_log_trace},
  {"debug", l_log_debug},
  {"info", l_log_info},
  {"warn", l_log_warn},
  {"error", l_log_error},
  {"fatal", l_log_fatal},
  {NULL, NULL}};

static int l_log_index(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "level")) {
    int level = log_get_level_fp(lfm->log_fp);
    lua_pushinteger(L, level);
    return 1;
  } else {
    return luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}

static int l_log_newindex(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "level")) {
    long level = lua_tointeger(L, 3);
    luaL_argcheck(L, level >= LOG_TRACE && level <= LOG_FATAL, 1,
        "level must be between " STR(LOG_TRACE) " and " STR(LOG_FATAL));
    log_info("log level set to %d", level);
    log_set_level_fp(lfm->log_fp, level);
  } else {
    return luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}

static const struct luaL_Reg log_mt[] = {
  {"__index", l_log_index},
  {"__newindex", l_log_newindex},
  {NULL, NULL}};

int luaopen_log(lua_State *L)
{
  lua_newtable(L);

  lua_pushnumber(L, LOG_TRACE);
  lua_setfield(L, -2, STR(LOG_TRACE));

  lua_pushnumber(L, LOG_DEBUG);
  lua_setfield(L, -2, STR(LOG_DEBUG));

  lua_pushnumber(L, LOG_INFO);
  lua_setfield(L, -2, STR(LOG_INFO));

  lua_pushnumber(L, LOG_WARN);
  lua_setfield(L, -2, STR(LOG_WARN));

  lua_pushnumber(L, LOG_ERROR);
  lua_setfield(L, -2, STR(LOG_ERROR));

  lua_pushinteger(L, LOG_FATAL);
  lua_setfield(L, -2, STR(LOG_FATAL));

  luaL_register(L, NULL, log_lib);

  luaL_newmetatable(L, LFM_LOG_META);
  luaL_register(L, NULL, log_mt);
  lua_setmetatable(L, -2);

  return 1;
}
