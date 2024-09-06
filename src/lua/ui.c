#include "private.h"

#include "../macros.h"

#include <ev.h>
#include <lauxlib.h>
#include <lua.h>

#include <stdint.h>
#include <wchar.h>

static int l_ui_messages(lua_State *L) {
  lua_createtable(L, vec_message_size(&ui->messages), 0);
  int i = 1;
  c_foreach(it, vec_message, ui->messages) {
    lua_pushstring(L, it.ref->text);
    lua_rawseti(L, -2, i);
    i++;
  }
  return 1;
}

static int l_ui_clear(lua_State *L) {
  (void)L;
  ui_clear(ui);
  return 0;
}

static int l_ui_get_width(lua_State *L) {
  lua_pushnumber(L, ui->x);
  return 1;
}

static int l_ui_get_height(lua_State *L) {
  lua_pushnumber(L, ui->y);
  return 1;
}

static int l_ui_menu(lua_State *L) {
  vec_str menu = vec_str_init();
  uint32_t delay = 0;
  if (lua_type(L, 1) == LUA_TTABLE) {
    const int n = lua_objlen(L, 1);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, 1, i);
      vec_str_emplace(&menu, lua_tostring(L, -1));
      lua_pop(L, 1);
    }
    if (lua_gettop(L) == 2) {
      luaL_checktype(L, 2, LUA_TNUMBER);
      int d = lua_tonumber(L, 2);
      luaL_argcheck(L, d >= 0, 2, "delay must be non-negative");
      delay = d;
    }
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    const char *str = lua_tostring(L, 1);
    for (const char *nl; (nl = strchr(str, '\n')); str = nl + 1) {
      vec_str_push(&menu, strndup(str, nl - str));
    }
    vec_str_emplace(&menu, str);
  }
  ui_menu_show(ui, &menu, delay);
  return 0;
}

static int l_ui_redraw(lua_State *L) {
  if (lua_gettop(L) > 0 && lua_toboolean(L, 1)) {
    ui_redraw(ui, REDRAW_FULL);
  }
  ev_idle_start(lfm->loop, &ui->redraw_watcher);
  return 0;
}

static int l_notcurses_canopen_images(lua_State *L) {
  lua_pushboolean(L, notcurses_canopen_images(ui->nc));
  return 1;
}

static int l_notcurses_canbraille(lua_State *L) {
  lua_pushboolean(L, notcurses_canbraille(ui->nc));
  return 1;
}

static int l_notcurses_canpixel(lua_State *L) {
  lua_pushboolean(L, notcurses_canpixel(ui->nc));
  return 1;
}

static int l_notcurses_canquadrant(lua_State *L) {
  lua_pushboolean(L, notcurses_canquadrant(ui->nc));
  return 1;
}

static int l_notcurses_cansextant(lua_State *L) {
  lua_pushboolean(L, notcurses_cansextant(ui->nc));
  return 1;
}

static int l_notcurses_canhalfblock(lua_State *L) {
  lua_pushboolean(L, notcurses_canhalfblock(ui->nc));
  return 1;
}

static int l_macro_recording(lua_State *L) {
  lua_pushboolean(L, macro_recording);
  return 1;
}

static int l_macro_record(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  wchar_t w;
  if (mbtowc(&w, str, MB_LEN_MAX) == -1) {
    return luaL_error(L, "converting to wchar_t");
  }
  if (macro_record(w)) {
    return luaL_error(L, "already recording recording");
  }
  return 1;
}

static int l_macro_stop_record(lua_State *L) {
  if (macro_stop_record()) {
    return luaL_error(L, "currently not recording");
  }
  return 0;
}

static int l_macro_play(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  wchar_t w;
  if (mbtowc(&w, str, MB_LEN_MAX) == -1) {
    return luaL_error(L, "converting to wchar_t");
  }
  if (macro_play(w, lfm)) {
    return luaL_error(L, "no such macro");
  }
  return 0;
}

static const struct luaL_Reg ui_lib[] = {
    {"macro_recording", l_macro_recording},
    {"macro_record", l_macro_record},
    {"macro_stop_record", l_macro_stop_record},
    {"macro_play", l_macro_play},
    {"notcurses_canopen_images", l_notcurses_canopen_images},
    {"notcurses_canhalfblock", l_notcurses_canhalfblock},
    {"notcurses_canquadrant", l_notcurses_canquadrant},
    {"notcurses_cansextant", l_notcurses_cansextant},
    {"notcurses_canbraille", l_notcurses_canbraille},
    {"notcurses_canpixel", l_notcurses_canpixel},
    {"get_width", l_ui_get_width},
    {"get_height", l_ui_get_height},
    {"clear", l_ui_clear},
    {"redraw", l_ui_redraw},
    {"menu", l_ui_menu},
    {"messages", l_ui_messages},
    {NULL, NULL}};

/* }}} */
int luaopen_ui(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, ui_lib);
  return 1;
}
