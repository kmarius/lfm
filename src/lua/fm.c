#include "../fm.h"
#include "../config.h"
#include "../hooks.h"
#include "../macros.h"
#include "../path.h"
#include "../search.h"
#include "../ui.h"
#include "private.h"

#include <lauxlib.h>
#include <lua.h>

#include <linux/limits.h>

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
  char buf[PATH_MAX];
  size_t len;
  const char *path = luaL_checklstring(L, 1, &len);
  if (len > PATH_MAX) {
    return luaL_error(L, "path too long");
  }
  loader_dir_from_path(&lfm->loader, path_normalize(path, fm->pwd, buf));
  return 0;
}

static int l_fm_sel(lua_State *L) {
  fm_move_cursor_to(fm, luaL_checkstring(L, 1));
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_up(lua_State *L) {
  if (fm_up(fm, luaL_optint(L, 1, 1))) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_down(lua_State *L) {
  if (fm_down(fm, luaL_optint(L, 1, 1))) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_top(lua_State *L) {
  (void)L;
  if (fm_top(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_up(lua_State *L) {
  (void)L;
  if (fm_scroll_up(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_down(lua_State *L) {
  (void)L;
  if (fm_scroll_down(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_bot(lua_State *L) {
  (void)L;
  if (fm_bot(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_updir(lua_State *L) {
  (void)L;
  if (fm_updir(fm)) {
    lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST);
    search_nohighlight(lfm);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_open(lua_State *L) {
  lfm_mode_exit(lfm, "visual");
  File *file = fm_open(fm);
  if (file) {
    if (cfg.selfile) {
      fm_selection_write(&lfm->fm, cfg.selfile);
      return lua_quit(L, lfm);
    }

    lua_pushstring(L, file_path(file));
    return 1;
  } else {
    /* changed directory */
    lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST);
    ui_redraw(ui, REDRAW_FM);
    search_nohighlight(lfm);
    return 0;
  }
}

static int l_fm_current_file(lua_State *L) {
  File *file = fm_current_file(fm);
  if (file) {
    lua_pushstring(L, file_path(file));
    return 1;
  }
  return 0;
}

static int l_fm_current_dir(lua_State *L) {
  const Dir *dir = fm_current_dir(fm);
  lua_createtable(L, 0, 3);

  lua_pushstring(L, dir->path);
  lua_setfield(L, -2, "path");

  lua_pushstring(L, dir->name);
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
    lua_pushstring(L, file_path(dir->files[i]));
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
    fm_move_cursor_to(fm, file_name(file));
  }
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
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    luaL_checktype(L, -1, LUA_TSTRING);
    size_t len;
    const char *path = luaL_checklstring(L, -1, &len);
    if (len > PATH_MAX) {
      luaL_error(L, "path too long");
    }
    fm_selection_add(fm, path_normalize(path, fm->pwd, buf), false);
    lua_pop(L, 1);
  }
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
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      fm_selection_add(
          fm, path_normalize(luaL_checkstring(L, -1), fm->pwd, buf), false);
    }
  }
  lfm_run_hook(lfm, LFM_HOOK_SELECTION);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_get(lua_State *L) {
  lua_createtable(L, pathlist_size(&fm->selection.current), 0);
  int i = 1;
  c_foreach(it, pathlist, fm->selection.current) {
    lua_pushstring(L, *it.ref);
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
  const char *arg = luaL_optstring(L, 1, "~");
  const char *last_slash = strchr(arg, '/');
  bool should_save = (arg[0] == '/' || arg[0] == '~' ||
                      (last_slash != NULL && last_slash[1] != 0));
  char *path = path_normalize_a(arg, fm->pwd);
  search_nohighlight(lfm);
  lfm_mode_exit(lfm, "visual");
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE);
  if (macro_playing) {
    fm_sync_chdir(fm, path, should_save, true);
  } else {
    fm_async_chdir(fm, path, should_save, true);
  }
  ui_redraw(ui, REDRAW_FM);
  xfree(path);
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
    lua_pushstring(L, *it.ref);
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
    for (size_t i = 0; i < l; i++) {
      lua_rawgeti(L, 1, i + 1);
      fm_paste_buffer_add(fm, lua_tostring(L, -1));
      lua_pop(L, 1);
    }
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
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_jump_automark(lua_State *L) {
  (void)L;
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE);
  lfm_mode_exit(lfm, "visual");
  fm_jump_automark(fm);
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

static const struct luaL_Reg fm_lib[] = {
    {"set_info", l_fm_set_info},
    {"get_info", l_fm_get_info},
    {"flatten", l_fm_flatten},
    {"flatten_level", l_fm_flatten_level},
    {"bottom", l_fm_bot},
    {"chdir", l_fm_chdir},
    {"down", l_fm_down},
    {"filter", l_fm_filter},
    {"getfilter", l_fm_filter_get},
    {"jump_automark", l_fm_jump_automark},
    {"open", l_fm_open},
    {"current_dir", l_fm_current_dir},
    {"current_file", l_fm_current_file},
    {"selection_reverse", l_fm_selection_reverse},
    {"selection_toggle", l_fm_selection_toggle_current},
    {"selection_add", l_fm_selection_add},
    {"selection_set", l_fm_selection_set},
    {"selection_get", l_fm_selection_get},
    {"selection_restore", l_fm_selection_restore},
    {"sort", l_fm_sort},
    {"top", l_fm_top},
    {"updir", l_fm_updir},
    {"up", l_fm_up},
    {"scroll_down", l_fm_scroll_down},
    {"scroll_up", l_fm_scroll_up},
    {"paste_buffer_get", l_fm_paste_buffer_get},
    {"paste_buffer_set", l_fm_paste_buffer_set},
    {"paste_mode_get", l_fm_paste_mode_get},
    {"paste_mode_set", l_fm_paste_mode_set},
    {"cut", l_fm_cut},
    {"copy", l_fm_copy},
    {"check", l_fm_check},
    {"load", l_fm_load},
    {"drop_cache", l_fm_drop_cache},
    {"reload", l_fm_reload},
    {"sel", l_fm_sel},
    {"get_height", l_fm_get_height},
    {NULL, NULL}};

int luaopen_fm(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, fm_lib);
  return 0;
}
