#include "lfm.h"

#include "auto/versiondef.h"
#include "config.h"
#include "defs.h"
#include "lfmlib.h"
#include "private.h"
#include "search.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// spawn.c
int l_spawn(lua_State *L);

// execute.c
int l_execute(lua_State *L);

static int l_schedule(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);
  int delay = luaL_optinteger(L, 2, 0);
  if (delay < 0)
    delay = 0;
  u32 id = lfm_schedule(lfm, lua_register_callback(L, 1), delay);
  lua_pushinteger(L, id);
  return 1;
}

static int l_cancel(lua_State *L) {
  u32 id = luaL_checkinteger(L, 1);
  lfm_cancel(lfm, id);
  return 0;
}

static int l_colors_clear(lua_State *L) {
  (void)L;
  config_colors_clear();
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_search(lua_State *L) {
  search(lfm, lua_tozsview(L, 1), true);
  return 0;
}

static int l_search_backwards(lua_State *L) {
  search(lfm, lua_tozsview(L, 1), false);
  return 0;
}

static int l_nohighlight(lua_State *L) {
  (void)L;
  search_nohighlight(lfm);
  return 0;
}

static int l_search_next(lua_State *L) {
  search_next(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_search_prev(lua_State *L) {
  search_prev(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_crash(lua_State *L) {
  xfree(L);
  return 0;
}

static int l_quit(lua_State *L) {
  return lua_quit(L, lfm);
}

static int l_print(lua_State *L) {
  int n = lua_gettop(L);
  lua_getglobal(L, "tostring");
  usize bufsz = 128;
  char *buf = xcalloc(bufsz, 1);
  usize ind = 0;
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, -1);
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    usize len;
    const char *s = lua_tolstring(L, -1, &len);
    if (s == NULL) {
      xfree(buf);
      return luaL_error(
          L, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
    }
    if (i > 1) {
      buf[ind++] = '\t';
    }
    if (ind + len >= bufsz) {
      do {
        bufsz *= 2;
      } while (ind + len >= bufsz);
      buf = xrealloc(buf, bufsz);
    }
    strncpy(&buf[ind], s, len);
    ind += len;
    lua_pop(L, 1); /* pop result */
  }
  buf[ind++] = 0;
  lfm_printf(lfm, "%s", buf);
  xfree(buf);
  return 0;
}

static int l_error(lua_State *L) {
  lfm_errorf(lfm, "%s", luaL_optstring(L, 1, ""));
  return 0;
}

static int l_message_clear(lua_State *L) {
  (void)L;
  ui->show_message = false;
  ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_print2(lua_State *L) {
  struct message msg = {};

  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "timeout");
    if (!lua_isnil(L, -1))
      msg.timeout = luaL_checknumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 2, "error");
    if (!lua_isnil(L, -1))
      msg.error = lua_toboolean(L, -1);

    lua_pop(L, 1);
  }

  msg.text = lua_tocstr(L, 1);

  ui_display_message(ui, msg);
  return 0;
}

static int l_thread(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 3);

  int ref = 0;
  if (lua_type(L, 1) == LUA_TFUNCTION) {
    // try to string.dump the function and insert it at position 1
    // TODO: we could store a ref to encode
    if (lua_string_dump(L, 1)) {
      return lua_error(L);
    }
    lua_replace(L, 1);
  }
  luaL_checktype(L, 1, LUA_TSTRING);
  if (lua_gettop(L) > 1) {
    if (!lua_isnoneornil(L, 2)) {
      ref = lua_register_callback(L, 2);
    }
  }
  bytes chunk = lua_tobytes(L, 1);
  bytes arg = bytes_init();
  if (lua_gettop(L) == 3) {
    // encode optional argument
    if (lua_encode(L, 3, &arg)) {
      bytes_drop(&chunk);
      return lua_error(L);
    }
  }
  async_lua(&lfm->async, &chunk, &arg, ref);
  return 0;
}

static const struct luaL_Reg lfm_lib[] = {
    {"schedule",      l_schedule        },
    {"cancel",        l_cancel          },
    {"colors_clear",  l_colors_clear    },
    {"execute",       l_execute         },
    {"spawn",         l_spawn           },
    {"thread",        l_thread          },
    {"nohighlight",   l_nohighlight     },
    {"search",        l_search          },
    {"search_back",   l_search_backwards},
    {"search_next",   l_search_next     },
    {"search_prev",   l_search_prev     },
    {"crash",         l_crash           },
    {"error",         l_error           },
    {"message_clear", l_message_clear   },
    {"print2",        l_print2          },
    {"quit",          l_quit            },
    {NULL,            NULL              },
};

int luaopen_lfm(lua_State *L) {
  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");

  luaL_openlib(L, "lfm", lfm_lib, 0); // [lfm]

  luaopen_options(L);
  lua_setfield(L, -2, "o");

  luaopen_api(L);
  lua_setfield(L, -2, "api");

  luaopen_fm(L);
  lua_setfield(L, -2, "fm");

  luaopen_paths(L);
  lua_setfield(L, -2, "paths");

  luaopen_log(L);
  lua_setfield(L, -2, "log");

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");

  luaopen_rifle(L);
  lua_setfield(L, -2, "rifle");

  lua_newtable(L);
  lua_pushzsview(L, c_zv(LFM_VERSION));
  lua_setfield(L, -2, "info");

  lua_pushzsview(L, c_zv(LFM_REVCOUNT));
  lua_setfield(L, -2, "revcount");

  lua_pushzsview(L, c_zv(LFM_COMMIT));
  lua_setfield(L, -2, "commit");

  lua_pushzsview(L, c_zv(LFM_BUILD_TYPE));
  lua_setfield(L, -2, "build_type");

  lua_pushzsview(L, c_zv(LFM_BRANCH));
  lua_setfield(L, -2, "branch");
  lua_setfield(L, -2, "version");

  lua_pop(L, 1); // []

  return 1;
}
