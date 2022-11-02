#include <lua.h>
#include <lauxlib.h>

#include "internal.h"
#include "../cmdline.h"
#include "../ui.h"

static int l_cmd_line_get(lua_State *L)
{
  lua_pushstring(L, cmdline_get(&ui->cmdline));
  return 1;
}

// TODO: when setting the prefix we also need to enable the cursor
// see ui_cmdline_prefix_set (on 2022-03-28)
static int l_cmd_line_set(lua_State *L)
{
  ui->show_message = false;

  if (lua_gettop(L) > 3) {
    luaL_error(L, "line_get takes up to three arguments");
  }

  switch (lua_gettop(L)) {
    case 1:
      if (cmdline_set(&ui->cmdline, lua_tostring(L, 1))) {
        ui_redraw(ui, REDRAW_CMDLINE);
      }
      break;
    case 2:
      // TODO: should this just set left/right and keep the prefix?
      // also: document it.
      if (cmdline_set_whole(&ui->cmdline, lua_tostring(L, 1),
            lua_tostring(L, 2), "")) {
        ui_redraw(ui, REDRAW_CMDLINE);
      }
      break;
    case 3:
      if (cmdline_set_whole(&ui->cmdline, lua_tostring(L, 1),
            lua_tostring(L, 2), lua_tostring(L, 3))) {
        ui_redraw(ui, REDRAW_CMDLINE);
      }
      break;
  }
  return 0;
}

static int l_cmd_toggle_overwrite(lua_State *L)
{
  (void) L;
  if (cmdline_toggle_overwrite(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_clear(lua_State *L)
{
  (void) L;
  ui_cmd_clear(ui);
  return 0;
}

static int l_cmd_delete(lua_State *L)
{
  (void) L;
  ui_cmd_delete(ui);
  return 0;
}

static int l_cmd_delete_right(lua_State *L)
{
  (void) L;
  if (cmdline_delete_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_delete_word(lua_State *L)
{
  (void) L;
  if (cmdline_delete_word(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_insert(lua_State *L)
{
  if (cmdline_insert(&ui->cmdline, lua_tostring(L, 1))) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_left(lua_State *L)
{
  (void) L;
  if (cmdline_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_right(lua_State *L)
{
  (void) L;
  if (cmdline_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_word_left(lua_State *L)
{
  (void) L;
  if (cmdline_word_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_word_right(lua_State *L)
{
  (void) L;
  if (cmdline_word_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_delete_line_left(lua_State *L)
{
  (void) L;
  if (cmdline_delete_line_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_home(lua_State *L)
{
  (void) L;
  if (cmdline_home(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_end(lua_State *L)
{
  (void) L;
  if (cmdline_end(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_prefix_set(lua_State *L)
{
  ui_cmd_prefix_set(ui, luaL_optstring(L, 1, ""));
  return 0;
}

static int l_cmd_prefix_get(lua_State *L)
{
  const char *prefix = cmdline_prefix_get(&ui->cmdline);
  lua_pushstring(L, prefix ? prefix : "");
  return 1;
}

static const struct luaL_Reg lfm_cmd_lib[] = {
  {"clear", l_cmd_clear},
  {"delete", l_cmd_delete},
  {"delete_right", l_cmd_delete_right},
  {"delete_word", l_cmd_delete_word},
  {"_end", l_cmd_end},
  {"line_get", l_cmd_line_get},
  {"line_set", l_cmd_line_set},
  {"prefix_get", l_cmd_prefix_get},
  {"prefix_set", l_cmd_prefix_set},
  {"home", l_cmd_home},
  {"insert", l_cmd_insert},
  {"toggle_overwrite", l_cmd_toggle_overwrite},
  {"left", l_cmd_left},
  {"word_left", l_cmd_word_left},
  {"word_right", l_cmd_word_right},
  {"delete_line_left", l_cmd_delete_line_left},
  {"right", l_cmd_right},
  {NULL, NULL}};

int luaopen_cmd(lua_State *L)
{
  lua_newtable(L);
  luaL_register(L, NULL, lfm_cmd_lib);
  return 1;
}
