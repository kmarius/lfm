#include "../cmdline.h"
#include "../containers.h"
#include "../fm.h"
#include "../history.h"
#include "../hooks.h"
#include "../macros.h"
#include "../path.h"
#include "../search.h"
#include "../stc/cstr.h"
#include "../ui.h"

#include "private.h"
#include "util.h"

#include <ev.h>
#include <lauxlib.h>
#include <locale.h>
#include <lua.h>

#include <linux/limits.h>
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <wchar.h>

static int l_cmd_line_get(lua_State *L) {
  lua_pushzsview(L, cmdline_get(&ui->cmdline));
  return 1;
}

static int l_cmd_line_set(lua_State *L) {
  ui->show_message = false;

  if (lua_gettop(L) > 2) {
    luaL_error(L, "line_set takes only up to two arguments");
  }

  cmdline_set(&ui->cmdline, lua_tozsview(L, 1), lua_tozsview(L, 2));
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
  if (cmdline_insert(&ui->cmdline, lua_tozsview(L, 1))) {
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
  const char *line = history_next_entry(&ui->cmdline.history);
  if (!line) {
    return 0;
  }
  lua_pushstring(L, line);
  return 1;
}

static int l_cmd_get_history(lua_State *L) {
  int i = history_size(&ui->cmdline.history);
  lua_createtable(L, i, 0);
  c_foreach(it, history, lfm->ui.cmdline.history) {
    lua_pushstring(L, it.ref->line);
    lua_rawseti(L, -2, i--);
  }
  return 1;
}

static const struct luaL_Reg cmdline_funcs[] = {
    {"cmdline_clear", l_cmd_clear},
    {"cmdline_delete", l_cmd_delete},
    {"cmdline_delete_right", l_cmd_delete_right},
    {"cmdline_delete_word", l_cmd_delete_word},
    {"cmdline__end", l_cmd_end},
    {"cmdline_line_get", l_cmd_line_get},
    {"cmdline_line_set", l_cmd_line_set},
    {"cmdline_home", l_cmd_home},
    {"cmdline_insert", l_cmd_insert},
    {"cmdline_toggle_overwrite", l_cmd_toggle_overwrite},
    {"cmdline_left", l_cmd_left},
    {"cmdline_word_left", l_cmd_word_left},
    {"cmdline_word_right", l_cmd_word_right},
    {"cmdline_delete_line_left", l_cmd_delete_line_left},
    {"cmdline_right", l_cmd_right},
    {"cmdline_history_append", l_cmd_history_append},
    {"cmdline_history_next", l_cmd_history_next},
    {"cmdline_history_prev", l_cmd_history_prev},
    {"cmdline_get_history", l_cmd_get_history},
    {NULL, NULL}};

static int l_fm_get_height(lua_State *L) {
  lua_pushnumber(L, fm->height);
  return 1;
}

static int l_fm_drop_cache(lua_State *L) {
  (void)L;
  fm_drop_cache(fm);
  ui_drop_cache(ui);
  return 0;
}

static int l_fm_reload(lua_State *L) {
  (void)L;
  fm_reload(fm);
  return 0;
}

static int l_fm_check(lua_State *L) {
  (void)L;
  Dir *d = fm_current_dir(fm);
  if (!dir_check(d)) {
    async_dir_load(&lfm->async, d, true);
  }
  return 0;
}

static int l_fm_load(lua_State *L) {
  char buf[PATH_MAX + 1];
  size_t len;
  const char *path = luaL_checklstring(L, 1, &len);
  const char *normalized =
      path_normalize(path, fm_getpwd_str(fm), buf, len, &len);
  if (normalized == NULL) {
    return luaL_error(L, "path too long");
  }
  loader_dir_from_path(&lfm->loader, zsview_from_n(normalized, len), true);
  return 0;
}

static int l_fm_sel(lua_State *L) {
  luaL_checktype(L, 1, LUA_TSTRING);
  fm_move_cursor_to(fm, lua_tozsview(L, 1));
  ui_update_file_preview(ui);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_up(lua_State *L) {
  if (fm_up(fm, luaL_optint(L, 1, 1))) {
    ui_update_file_preview_delayed(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_down(lua_State *L) {
  if (fm_down(fm, luaL_optint(L, 1, 1))) {
    ui_update_file_preview_delayed(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_top(lua_State *L) {
  (void)L;
  if (fm_top(fm)) {
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_up(lua_State *L) {
  (void)L;
  if (fm_scroll_up(fm)) {
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_down(lua_State *L) {
  (void)L;
  if (fm_scroll_down(fm)) {
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_bot(lua_State *L) {
  (void)L;
  if (fm_bot(fm)) {
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_updir(lua_State *L) {
  (void)L;
  if (fm_updir(fm)) {
    // I don't remember why we run th chdir post hook here,
    // since we are also not running the pre hook
    lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST, &fm->pwd);
    search_nohighlight(lfm);
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_open(lua_State *L) {
  lfm_mode_exit(lfm, "visual");
  File *file = fm_open(fm);
  if (file) {
    if (lfm->opts.selection_path) {
      fm_selection_write(&lfm->fm, lfm->opts.selection_path);
      return lua_quit(L, lfm);
    }

    lua_pushcstr(L, file_path(file));
    return 1;
  } else {
    /* changed directory */
    lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST, &fm->pwd);
    ui_update_file_preview(ui);
    ui_redraw(ui, REDRAW_FM);
    search_nohighlight(lfm);
    return 0;
  }
}

static int l_fm_current_file(lua_State *L) {
  File *file = fm_current_file(fm);
  if (file) {
    lua_pushcstr(L, file_path(file));
    return 1;
  }
  return 0;
}

static int l_fm_current_dir(lua_State *L) {
  const Dir *dir = fm_current_dir(fm);
  lua_createtable(L, 0, 3);

  lua_pushcstr(L, dir_path(dir));
  lua_setfield(L, -2, "path");

  lua_pushzsview(L, *dir_name(dir));
  lua_setfield(L, -2, "name");

  lua_createtable(L, 0, 3);
  lua_pushstring(L, sorttype_str[dir->settings.sorttype]);
  lua_setfield(L, -2, "type");
  lua_pushboolean(L, dir->settings.dirfirst);
  lua_setfield(L, -2, "dirfirst");
  lua_pushboolean(L, dir->settings.reverse);
  lua_setfield(L, -2, "reverse");
  lua_setfield(L, -2, "sortopts");

  lua_createtable(L, dir->length, 0);
  for (uint32_t i = 0; i < dir->length; i++) {
    lua_pushcstr(L, file_path(dir->files[i]));
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "files");

  return 1;
}

static int l_fm_get_info(lua_State *L) {
  Dir *dir = fm_current_dir(fm);
  lua_pushstring(L, fileinfo_str[dir->settings.fileinfo]);
  return 1;
}

static int l_fm_set_info(lua_State *L) {
  const char *val = luaL_checkstring(L, 1);
  Dir *dir = fm_current_dir(fm);
  for (int i = 0; i < NUM_FILEINFO; i++) {
    if (streq(val, fileinfo_str[i])) {
      dir->settings.fileinfo = i;
      ui_redraw(ui, REDRAW_FM);
      return 0;
    }
  }
  return luaL_error(L, "invalid option for info: %s", val);
}

static int l_fm_sort(lua_State *L) {
  lfm_mode_exit(lfm, "visual");
  luaL_checktype(L, 1, LUA_TTABLE);
  Dir *dir = fm_current_dir(fm);

  struct dir_settings settings = dir->settings;
  lua_getfield(L, 1, "dirfirst");
  if (!lua_isnoneornil(L, -1)) {
    settings.dirfirst = lua_toboolean(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "reverse");
  if (!lua_isnoneornil(L, -1)) {
    settings.reverse = lua_toboolean(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "type");
  if (!lua_isnoneornil(L, -1)) {
    const char *op = luaL_checkstring(L, -1);
    int j;
    for (j = 0; j < NUM_SORTTYPE; j++) {
      if (streq(op, sorttype_str[j])) {
        settings.sorttype = j;
        break;
      }
    }
    if (j == NUM_SORTTYPE) {
      return luaL_error(L, "unrecognized sort type: %s", op);
    }
  }
  lua_pop(L, 1);

  dir->settings = settings;

  const File *file = dir_current_file(dir);
  dir->sorted = false;
  dir_sort(dir);
  if (file && dir->dirty) {
    fm_move_cursor_to(fm, *file_name(file));
  }
  ui_update_file_preview(ui);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_toggle_current(lua_State *L) {
  (void)L;
  fm_selection_toggle_current(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_add(lua_State *L) {
  char buf[PATH_MAX];
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = lua_objlen(L, 1);
  cstr cs = cstr_init();
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);

    size_t len;
    const char *path = lua_tolstring(L, -1, &len);
    const char *normalized =
        path_normalize(path, fm_getpwd_str(fm), buf, len, &len);
    if (normalized == NULL) {
      cstr_drop(&cs);
      return luaL_error(L, "path too long");
    }
    cstr_assign_n(&cs, buf, len);
    fm_selection_add(fm, &cs, false);

    lua_pop(L, 1);
  }
  cstr_drop(&cs);
  if (n > 0) {
    lfm_run_hook(lfm, LFM_HOOK_SELECTION);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_selection_set(lua_State *L) {
  if (lua_gettop(L) > 0 && !lua_isnil(L, 1) && !lua_istable(L, 1)) {
    return luaL_argerror(L, 1, "table or nil required");
  }
  char buf[PATH_MAX];
  fm_selection_clear(fm);
  lfm_mode_exit(lfm, "visual");
  if (lua_istable(L, 1)) {
    cstr cs = cstr_init();
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      size_t len;
      const char *str = lua_tolstring(L, -1, &len);
      const char *normalized =
          path_normalize(str, fm_getpwd_str(fm), buf, len, &len);
      if (normalized == NULL) {
        cstr_drop(&cs);
        return luaL_error(L, "path too long");
      }
      cstr_assign_n(&cs, buf, len);
      fm_selection_add(fm, &cs, false);
    }
    cstr_drop(&cs);
  }
  lfm_run_hook(lfm, LFM_HOOK_SELECTION);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_get(lua_State *L) {
  lua_createtable(L, pathlist_size(&fm->selection.current), 0);
  int i = 1;
  c_foreach(it, pathlist, fm->selection.current) {
    lua_pushcstr(L, it.ref);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int l_fm_selection_reverse(lua_State *L) {
  (void)L;
  fm_selection_reverse(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_restore(lua_State *L) {
  (void)L;
  pathlist tmp = fm->selection.current;
  fm->selection.current = fm->selection.previous;
  fm->selection.previous = tmp;
  lfm_run_hook(lfm, LFM_HOOK_SELECTION);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_chdir(lua_State *L) {
  char buf[PATH_MAX + 1];
  size_t len;
  const char *arg = luaL_optlstring(L, 1, "~", &len);
  const char *last_slash = strchr(arg, '/');
  bool should_save = (arg[0] == '/' || arg[0] == '~' ||
                      (last_slash != NULL && last_slash[1] != 0));

  char *path = path_normalize(arg, fm_getpwd_str(fm), buf, len, NULL);
  if (path == NULL) {
    return luaL_error(L, "path too long");
  }

  search_nohighlight(lfm);
  lfm_mode_exit(lfm, "visual");
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE, &fm->pwd);
  if (macro_playing) {
    fm_sync_chdir(fm, path, should_save, true);
  } else {
    fm_async_chdir(fm, path, should_save, true);
  }
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_paste_mode_get(lua_State *L) {
  lua_pushstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy");
  return 1;
}

static int l_fm_paste_mode_set(lua_State *L) {
  const char *mode = luaL_checkstring(L, 1);
  paste_mode prev = fm->paste.mode;
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "unrecognized paste mode: %s", mode);
  }
  if (fm->paste.mode != prev) {
    lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  }
  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_fm_paste_buffer_get(lua_State *L) {
  lua_createtable(L, pathlist_size(&fm->paste.buffer), 0);
  int i = 1;
  c_foreach(it, pathlist, fm->paste.buffer) {
    lua_pushcstr(L, it.ref);
    lua_rawseti(L, -2, i++);
  }
  lua_pushstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy");
  return 2;
}

static int l_fm_paste_buffer_set(lua_State *L) {
  size_t prev_size = pathlist_size(&fm->paste.buffer);
  paste_mode prev_mode = fm->paste.mode;
  fm_paste_buffer_clear(fm);

  const char *mode = luaL_optstring(L, 2, "copy");
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "unrecognized paste mode: %s", mode);
  }

  if (lua_type(L, 1) == LUA_TTABLE) {
    const size_t l = lua_objlen(L, 1);
    cstr cs = cstr_init();
    size_t len;
    for (size_t i = 0; i < l; i++) {
      lua_rawgeti(L, 1, i + 1);
      const char *str = lua_tolstring(L, -1, &len);
      cstr_assign_n(&cs, str, len);
      fm_paste_buffer_add(fm, &cs);
      lua_pop(L, 1);
    }
    cstr_drop(&cs);
  }

  if (luaL_optbool(L, 3, true) &&
      (pathlist_size(&fm->paste.buffer) != prev_size ||
       fm->paste.mode != prev_mode)) {
    lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  }

  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_fm_copy(lua_State *L) {
  (void)L;
  lfm_mode_exit(lfm, "visual");
  fm_paste_mode_set(fm, PASTE_MODE_COPY);
  lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_cut(lua_State *L) {
  (void)L;
  lfm_mode_exit(lfm, "visual");
  fm_paste_mode_set(fm, PASTE_MODE_MOVE);
  lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_filter_get(lua_State *L) {
  Filter *filter = fm_current_dir(fm)->filter;
  if (filter) {
    lua_pushstring(L, filter_string(filter));
    lua_pushstring(L, filter_type(filter));
    return 2;
  }
  return 0;
}

static int l_fm_filter(lua_State *L) {
  if (!lua_isnoneornil(L, 1)) {
    const char *type = lua_tostring(L, 2);
    if (!type || streq(type, "substring")) {
      fm_filter(fm, filter_create_sub(lua_tostring(L, 1)));
    } else if (streq(type, "fuzzy")) {
      fm_filter(fm, filter_create_fuzzy(lua_tostring(L, 1)));
    } else if (streq(type, "lua")) {
      luaL_checktype(L, 1, LUA_TFUNCTION);
      lua_settop(L, 1);
      int ref = lua_set_callback(L);
      fm_filter(fm, filter_create_lua(ref, L));
    } else {
      return luaL_error(L, "unrecognized filter type: %s", type);
    }
  } else {
    fm_filter(fm, NULL);
  }
  ui_update_file_preview_delayed(ui);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_jump_automark(lua_State *L) {
  (void)L;
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE, &fm->pwd);
  lfm_mode_exit(lfm, "visual");
  fm_jump_automark(fm);
  ui_update_file_preview(ui);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_flatten_level(lua_State *L) {
  lua_pushinteger(L, fm_current_dir(fm)->flatten_level);
  return 1;
}

static int l_fm_flatten(lua_State *L) {
  int level = luaL_optinteger(L, 1, 0);
  if (level < 0) {
    level = 0;
  }
  fm_flatten(fm, level);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static const struct luaL_Reg fm_funcs[] = {
    {"fm_set_info", l_fm_set_info},
    {"fm_get_info", l_fm_get_info},
    {"fm_flatten", l_fm_flatten},
    {"fm_flatten_level", l_fm_flatten_level},
    {"fm_bottom", l_fm_bot},
    {"fm_chdir", l_fm_chdir},
    {"fm_down", l_fm_down},
    {"fm_filter", l_fm_filter},
    {"fm_getfilter", l_fm_filter_get},
    {"fm_jump_automark", l_fm_jump_automark},
    {"fm_open", l_fm_open},
    {"fm_current_dir", l_fm_current_dir},
    {"fm_current_file", l_fm_current_file},
    {"fm_selection_reverse", l_fm_selection_reverse},
    {"fm_selection_toggle", l_fm_selection_toggle_current},
    {"fm_selection_add", l_fm_selection_add},
    {"fm_selection_set", l_fm_selection_set},
    {"fm_selection_get", l_fm_selection_get},
    {"fm_selection_restore", l_fm_selection_restore},
    {"fm_sort", l_fm_sort},
    {"fm_top", l_fm_top},
    {"fm_updir", l_fm_updir},
    {"fm_up", l_fm_up},
    {"fm_scroll_down", l_fm_scroll_down},
    {"fm_scroll_up", l_fm_scroll_up},
    {"fm_paste_buffer_get", l_fm_paste_buffer_get},
    {"fm_paste_buffer_set", l_fm_paste_buffer_set},
    {"fm_paste_mode_get", l_fm_paste_mode_get},
    {"fm_paste_mode_set", l_fm_paste_mode_set},
    {"fm_cut", l_fm_cut},
    {"fm_copy", l_fm_copy},
    {"fm_check", l_fm_check},
    {"fm_load", l_fm_load},
    {"fm_drop_cache", l_fm_drop_cache},
    {"fm_reload", l_fm_reload},
    {"fm_sel", l_fm_sel},
    {"fm_get_height", l_fm_get_height},
    {NULL, NULL}};

struct notcurses **get_notcurses() {
  return &ui->nc;
}

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
  vec_cstr menu = vec_cstr_init();
  uint32_t delay = 0;
  if (lua_type(L, 1) == LUA_TTABLE) {
    lua_read_vec_cstr(L, 1, &menu);

    if (lua_gettop(L) == 2) {
      luaL_checktype(L, 2, LUA_TNUMBER);
      int d = lua_tonumber(L, 2);
      luaL_argcheck(L, d >= 0, 2, "delay must be non-negative");
      delay = d;
    }
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    const char *str = lua_tostring(L, 1);
    for (const char *nl; (nl = strchr(str, '\n')); str = nl + 1) {
      vec_cstr_push(&menu, cstr_with_n(str, nl - str));
    }
    vec_cstr_emplace(&menu, str);
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

static int l_notcurses_palette_size(lua_State *L) {
  lua_pushnumber(L, notcurses_palette_size(ui->nc));
  return 1;
}

static int l_notcurses_cantruecolor(lua_State *L) {
  lua_pushboolean(L, notcurses_cantruecolor(ui->nc));
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

static const struct luaL_Reg ui_funcs[] = {
    {"ui_macro_recording", l_macro_recording},
    {"ui_macro_record", l_macro_record},
    {"ui_macro_stop_record", l_macro_stop_record},
    {"ui_macro_play", l_macro_play},
    {"notcurses_palette_size", l_notcurses_palette_size},
    {"notcurses_cantruecolor", l_notcurses_cantruecolor},
    {"ui_get_width", l_ui_get_width},
    {"ui_get_height", l_ui_get_height},
    {"ui_clear", l_ui_clear},
    {"ui_redraw", l_ui_redraw},
    {"ui_menu", l_ui_menu},
    {"ui_messages", l_ui_messages},
    {"notcurses_canopen_images", l_notcurses_canopen_images},
    {"notcurses_canhalfblock", l_notcurses_canhalfblock},
    {"notcurses_canquadrant", l_notcurses_canquadrant},
    {"notcurses_cansextant", l_notcurses_cansextant},
    {"notcurses_canbraille", l_notcurses_canbraille},
    {"notcurses_canpixel", l_notcurses_canpixel},
    {NULL, NULL}};

int luaopen_api(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, cmdline_funcs);
  luaL_register(L, NULL, fm_funcs);
  luaL_register(L, NULL, ui_funcs);
  return 1;
}
