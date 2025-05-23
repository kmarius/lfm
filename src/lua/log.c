#include "../log.h"
#include "private.h"

#include <lauxlib.h>
#include <lua.h>

#define LFM_LOG_META "Lfm.Log.Meta"
#define STR(x) #x

// lua_getstack fails if the stack is smaller, e.g. if called directly as a
// callback. We just log @callback:0 for file/line

static int l_log_trace(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_TRACE, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_TRACE, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_debug(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_DEBUG, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_DEBUG, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_info(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_INFO, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_INFO, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_warn(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_WARN, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_WARN, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_error(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_ERROR, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_ERROR, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_fatal(lua_State *L) {
  lua_Debug ar;
  if (lua_getstack(L, 2, &ar) == 0) {
    log_log(LOG_FATAL, "@callback", 0, "%s", luaL_checkstring(L, 1));
  } else {
    lua_getinfo(L, "Sl", &ar);
    log_log(LOG_FATAL, ar.source, ar.currentline, "%s", luaL_checkstring(L, 1));
  }
  return 0;
}

static int l_log_get_level(lua_State *L) {
  int level = log_get_level_fp(lfm->opts.log);
  lua_pushinteger(L, level);
  return 1;
}

static int l_log_set_level(lua_State *L) {
  long level = luaL_checkinteger(L, 1);
  luaL_argcheck(L, level >= LOG_TRACE && level <= LOG_FATAL, 1,
                "level must be between " STR(LOG_TRACE) " and " STR(LOG_FATAL));
  log_set_level_fp(lfm->opts.log, level);
  log_info("log level set to %d", level);
  return 0;
}

static const struct luaL_Reg log_lib[] = {{"trace", l_log_trace},
                                          {"debug", l_log_debug},
                                          {"info", l_log_info},
                                          {"warn", l_log_warn},
                                          {"error", l_log_error},
                                          {"fatal", l_log_fatal},
                                          {"set_level", l_log_set_level},
                                          {"get_level", l_log_get_level},
                                          {NULL, NULL}};

int luaopen_log(lua_State *L) {
  lua_newtable(L);

  lua_pushnumber(L, LOG_TRACE);
  lua_setfield(L, -2, STR(TRACE));

  lua_pushnumber(L, LOG_DEBUG);
  lua_setfield(L, -2, STR(DEBUG));

  lua_pushnumber(L, LOG_INFO);
  lua_setfield(L, -2, STR(INFO));

  lua_pushnumber(L, LOG_WARN);
  lua_setfield(L, -2, STR(WARN));

  lua_pushnumber(L, LOG_ERROR);
  lua_setfield(L, -2, STR(ERROR));

  lua_pushinteger(L, LOG_FATAL);
  lua_setfield(L, -2, STR(FATAL));

  luaL_register(L, NULL, log_lib);

  return 1;
}
