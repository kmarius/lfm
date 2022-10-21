#include <lauxlib.h>

#include "internal.h"
#include "../log.h"

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

int luaopen_log(lua_State *L)
{
  lua_newtable(L);
  luaL_register(L, NULL, log_lib);
  return 1;
}
