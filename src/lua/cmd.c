#include <lauxlib.h>
#include <lua.h>

#include "../cmdline.h"
#include "../ui.h"
#include "private.h"

static int l_cmd_line_get(lua_State *L) {
  lua_pushstring(L, cmdline_get(&ui->cmdline));
  return 1;
}

static int l_cmd_line_set(lua_State *L) {
  ui->show_message = false;

  if (lua_gettop(L) > 2) {
    luaL_error(L, "line_set takes only up to two arguments");
  }

  cmdline_set(&ui->cmdline, lua_tostring(L, 1), lua_tostring(L, 2));
  ui_redraw(ui, REDRAW_CMDLINE);

  return 0;
}

static int l_cmd_toggle_overwrite(lua_State *L) {
  (void)L;
  if (cmdline_toggle_overwrite(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_clear(lua_State *L) {
  (void)L;
  cmdline_clear(&ui->cmdline);
  return 0;
}

static int l_cmd_delete(lua_State *L) {
  (void)L;
  if (ui->cmdline.left.len == 0 && ui->cmdline.right.len == 0) {
    lfm_mode_enter(lfm, "normal");
  } else {
    cmdline_delete(&ui->cmdline);
    mode_on_change(lfm->current_mode, lfm);
  }
  ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_delete_right(lua_State *L) {
  (void)L;
  if (cmdline_delete_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
    mode_on_change(lfm->current_mode, lfm);
  }
  return 0;
}

static int l_cmd_delete_word(lua_State *L) {
  (void)L;
  if (cmdline_delete_word(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
    mode_on_change(lfm->current_mode, lfm);
  }
  return 0;
}

static int l_cmd_insert(lua_State *L) {
  if (cmdline_insert(&ui->cmdline, lua_tostring(L, 1))) {
    ui_redraw(ui, REDRAW_CMDLINE);
    mode_on_change(lfm->current_mode, lfm);
  }
  return 0;
}

static int l_cmd_left(lua_State *L) {
  (void)L;
  if (cmdline_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_right(lua_State *L) {
  (void)L;
  if (cmdline_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_word_left(lua_State *L) {
  (void)L;
  if (cmdline_word_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_word_right(lua_State *L) {
  (void)L;
  if (cmdline_word_right(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_delete_line_left(lua_State *L) {
  (void)L;
  if (cmdline_delete_line_left(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
    mode_on_change(lfm->current_mode, lfm);
  }
  return 0;
}

static int l_cmd_home(lua_State *L) {
  (void)L;
  if (cmdline_home(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_end(lua_State *L) {
  (void)L;
  if (cmdline_end(&ui->cmdline)) {
    ui_redraw(ui, REDRAW_CMDLINE);
  }
  return 0;
}

static int l_cmd_history_append(lua_State *L) {
  history_append(&ui->cmdline.history, luaL_checkstring(L, 1),
                 luaL_checkstring(L, 2));
  return 0;
}

static int l_cmd_history_prev(lua_State *L) {
  const char *line = history_prev(&ui->cmdline.history);
  if (!line) {
    return 0;
  }
  lua_pushstring(L, line);
  return 1;
}

static int l_cmd_history_next(lua_State *L) {
  const char *line = history_next(&ui->cmdline.history);
  if (!line) {
    return 0;
  }
  lua_pushstring(L, line);
  return 1;
}

static int l_cmd_get_history(lua_State *L) {
  int i = ui->cmdline.history.items.size;
  lua_createtable(L, i, 0);
  lht_foreach(struct history_entry * e, &ui->cmdline.history.items) {
    lua_pushstring(L, e->line);
    lua_rawseti(L, -2, i--);
  }
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
    {"home", l_cmd_home},
    {"insert", l_cmd_insert},
    {"toggle_overwrite", l_cmd_toggle_overwrite},
    {"left", l_cmd_left},
    {"word_left", l_cmd_word_left},
    {"word_right", l_cmd_word_right},
    {"delete_line_left", l_cmd_delete_line_left},
    {"right", l_cmd_right},
    {"history_append", l_cmd_history_append},
    {"history_next", l_cmd_history_next},
    {"history_prev", l_cmd_history_prev},
    {"get_history", l_cmd_get_history},
    {NULL, NULL}};

int luaopen_cmd(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, lfm_cmd_lib);
  return 1;
}
