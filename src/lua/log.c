#include "../log.h"
#include "../config.h"
#include "private.h"

#include <lauxlib.h>
#include <lua.h>

#define LFM_LOG_META "Lfm.Log.Meta"
#define STR(x) #x

// lua_getstack fails if the stack is smaller, e.g. if called directly as a
// callback. We just log @callback:0 for file/line

static char luadir[60];
static int luadir_len;
#define DO_LOG(level)                                                          \
  do {                                                                         \
    lua_Debug ar;                                                              \
    if (lua_getstack(L, 2, &ar) == 0) {                                        \
      log_log((level), "Callback", 0, "%s", luaL_checkstring(L, 1));           \
    } else {                                                                   \
      char buf[64];                                                            \
      lua_getinfo(L, "Sl", &ar);                                               \
      const char *source = ar.source;                                          \
      if (zsview_starts_with(zsview_from(source), luadir)) {                   \
        snprintf(buf, sizeof buf - 1, "@%s", &ar.source[luadir_len]);          \
        source = buf;                                                          \
      }                                                                        \
      log_log((level), source, ar.currentline, "%s", luaL_checkstring(L, 1));  \
    }                                                                          \
  } while (false);

static int l_log_trace(lua_State *L) {
  DO_LOG(LOG_TRACE);
  return 0;
}

static int l_log_debug(lua_State *L) {
  DO_LOG(LOG_DEBUG);
  return 0;
}

static int l_log_info(lua_State *L) {
  DO_LOG(LOG_INFO);
  return 0;
}

static int l_log_warn(lua_State *L) {
  DO_LOG(LOG_WARN);
  return 0;
}

static int l_log_error(lua_State *L) {
  DO_LOG(LOG_ERROR);
  return 0;
}

static int l_log_fatal(lua_State *L) {
  DO_LOG(LOG_ERROR);
  return 0;
}

static int l_log_custom(lua_State *L) {
  long level = luaL_checkinteger(L, 1);
  luaL_argcheck(L, level >= LOG_TRACE && level <= LOG_FATAL, 1,
                "level must be between " STR(LOG_TRACE) " and " STR(LOG_FATAL));
  const char *source = luaL_optstring(L, 3, "");
  int line = luaL_optinteger(L, 4, 0);
  log_log(level, source, line, "%s", luaL_checkstring(L, 2));
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

static const struct luaL_Reg log_lib[] = {
    {"trace",     l_log_trace    },
    {"debug",     l_log_debug    },
    {"info",      l_log_info     },
    {"warn",      l_log_warn     },
    {"error",     l_log_error    },
    {"fatal",     l_log_fatal    },
    {"custom",    l_log_custom   },
    {"set_level", l_log_set_level},
    {"get_level", l_log_get_level},
    {NULL,        NULL           },
};

int luaopen_log(lua_State *L) {
  lua_newtable(L);

  luadir_len = snprintf(luadir, sizeof luadir - 1, "@%s/%s/",
                        cstr_str(&cfg.configdir), "lua");

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
