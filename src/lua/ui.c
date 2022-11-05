#include <lauxlib.h>

#include "internal.h"

static int l_ui_messages(lua_State *L)
{
  lua_newtable(L);
  for (size_t i = 0; i < cvector_size(ui->messages); i++) {
    lua_pushstring(L, ui->messages[i].text);
    lua_rawseti(L, -2, i+1);
  }
  return 1;
}

static int l_ui_clear(lua_State *L)
{
  (void) L;
  ui_clear(ui);
  return 0;
}

static int l_ui_get_width(lua_State *L)
{
  lua_pushnumber(L, ui->ncol);
  return 1;
}

static int l_ui_get_height(lua_State *L)
{
  lua_pushnumber(L, ui->nrow);
  return 1;
}

static int l_ui_menu(lua_State *L)
{
  char **menubuf = NULL;
  if (lua_type(L, 1) == LUA_TTABLE) {
    const int n = lua_objlen(L, 1);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, 1, i);
      cvector_push_back(menubuf, strdup(lua_tostring(L, -1)));
      lua_pop(L, 1);
    }
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    const char *str = lua_tostring(L, 1);
    for (const char *nl; (nl = strchr(str, '\n')); str = nl + 1) {
      cvector_push_back(menubuf, strndup(str, nl - str));
    }
    cvector_push_back(menubuf, strdup(str));
  }
  ui_menu_show(ui, menubuf);
  return 0;
}

static int l_ui_draw(lua_State *L)
{
  (void) L;
  ui_redraw(ui, REDRAW_FULL);
  return 0;
}

static int l_notcurses_canopen_images(lua_State *L)
{
  lua_pushboolean(L, notcurses_canopen_images(ui->nc));
  return 1;
}

static int l_notcurses_canbraille(lua_State *L)
{
  lua_pushboolean(L, notcurses_canbraille(ui->nc));
  return 1;
}

static int l_notcurses_canpixel(lua_State *L)
{
  lua_pushboolean(L, notcurses_canpixel(ui->nc));
  return 1;
}

static int l_notcurses_canquadrant(lua_State *L)
{
  lua_pushboolean(L, notcurses_canquadrant(ui->nc));
  return 1;
}

static int l_notcurses_cansextant(lua_State *L)
{
  lua_pushboolean(L, notcurses_cansextant(ui->nc));
  return 1;
}

static int l_notcurses_canhalfblock(lua_State *L)
{
  lua_pushboolean(L, notcurses_canhalfblock(ui->nc));
  return 1;
}

static const struct luaL_Reg ui_lib[] = {
  {"notcurses_canopen_images", l_notcurses_canopen_images},
  {"notcurses_canhalfblock", l_notcurses_canhalfblock},
  {"notcurses_canquadrant", l_notcurses_canquadrant},
  {"notcurses_cansextant", l_notcurses_cansextant},
  {"notcurses_canbraille", l_notcurses_canbraille},
  {"notcurses_canpixel", l_notcurses_canpixel},
  {"get_width", l_ui_get_width},
  {"get_height", l_ui_get_height},
  {"clear", l_ui_clear},
  {"draw", l_ui_draw},
  {"menu", l_ui_menu},
  {"messages", l_ui_messages},
  {NULL, NULL}};

/* }}} */
int luaopen_ui(lua_State *L)
{
  lua_newtable(L);
  luaL_register(L, NULL, ui_lib);
  return 1;
}
