#include <lua.h>
#include <lauxlib.h>

#include "internal.h"
#include "../config.h"
#include "../fm.h"
#include "../hooks.h"
#include "../log.h"
#include "../search.h"
#include "../ui.h"

static int l_fm_get_height(lua_State *L)
{
  lua_pushnumber(L, fm->height);
  return 1;
}

static int l_fm_drop_cache(lua_State *L)
{
  (void) L;
  fm_drop_cache(fm);
  ui_drop_cache(ui);
  return 0;
}

static int l_fm_reload(lua_State *L)
{
  (void) L;
  fm_reload(fm);
  return 0;
}

static int l_fm_check(lua_State *L)
{
  (void) L;
  Dir *d = fm_current_dir(fm);
  if (!dir_check(d)) {
    async_dir_load(&lfm->async, d, true);
  }
  return 0;
}

static int l_fm_load(lua_State *L)
{
  loader_dir_from_path(&lfm->loader, luaL_checkstring(L, 1));
  return 0;
}

static int l_fm_sel(lua_State *L)
{
  fm_move_cursor_to(fm, luaL_checkstring(L, 1));
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_up(lua_State *L)
{
  if (fm_up(fm, luaL_optint(L, 1, 1))) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_down(lua_State *L)
{
  if (fm_down(fm, luaL_optint(L, 1, 1))) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_top(lua_State *L)
{
  (void) L;
  if (fm_top(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_up(lua_State *L)
{
  (void) L;
  if (fm_scroll_up(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_scroll_down(lua_State *L)
{
  (void) L;
  if (fm_scroll_down(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_bot(lua_State *L)
{
  (void) L;
  if (fm_bot(fm)) {
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_updir(lua_State *L)
{
  (void) L;
  if (fm_updir(fm)) {
    lfm_run_hook(lfm, LFM_HOOK_CHDIRPOST);
    search_nohighlight(lfm);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_fm_open(lua_State *L)
{
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

static int l_fm_current_file(lua_State *L)
{
  File *file = fm_current_file(fm);
  if (file) {
    lua_pushstring(L, file_path(file));
    return 1;
  }
  return 0;
}

static int l_fm_current_dir(lua_State *L)
{
  const Dir *dir = fm_current_dir(fm);
  lua_newtable(L);
  lua_pushstring(L, dir->path);
  lua_setfield(L, -2, "path");
  lua_pushstring(L, dir->name);
  lua_setfield(L, -2, "name");

  lua_newtable(L);
  for (uint32_t i = 0; i < dir->length; i++) {
    lua_pushstring(L, file_path(dir->files[i]));
    lua_rawseti(L, -2, i+1);
  }
  lua_setfield(L, -2, "files");

  return 1;
}

static int l_fm_visual_start(lua_State *L)
{
  (void) L;
  fm_selection_visual_start(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_visual_end(lua_State *L)
{
  (void) L;
  fm_selection_visual_stop(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_visual_toggle(lua_State *L)
{
  (void) L;
  fm_selection_visual_toggle(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_sortby(lua_State *L)
{
  const int l = lua_gettop(L);
  fm_selection_visual_stop(fm);
  Dir *dir = fm_current_dir(fm);
  for (int i = 1; i <= l; i++) {
    const char *op = luaL_checkstring(L, i);
    if (streq(op, "name")) {
      dir->settings.sorttype = SORT_NAME;
    } else if (streq(op, "natural")) {
      dir->settings.sorttype = SORT_NATURAL;
    } else if (streq(op, "ctime")) {
      dir->settings.sorttype = SORT_CTIME;
    } else if (streq(op, "size")) {
      dir->settings.sorttype = SORT_SIZE;
    } else if (streq(op, "random")) {
      dir->settings.sorttype = SORT_RAND;
    } else if (streq(op, "dirfirst")) {
      dir->settings.dirfirst = true;
    } else if (streq(op, "nodirfirst")) {
      dir->settings.dirfirst = false;
    } else if (streq(op, "reverse")) {
      dir->settings.reverse = true;
    } else if (streq(op, "noreverse")) {
      dir->settings.reverse = false;
    } else {
      return luaL_error(L, "sortby: unrecognized option: %s", op);
    }
  }
  dir->sorted = false;
  const File *file = dir_current_file(dir);
  dir_sort(dir);
  if (file) {
    fm_move_cursor_to(fm, file_name(file));
  }
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_toggle_current(lua_State *L)
{
  (void) L;
  fm_selection_toggle_current(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_add(lua_State *L)
{
  fm_selection_add(fm, luaL_checkstring(L, 1));
  return 0;
}

static int l_fm_selection_set(lua_State *L)
{
  if (lua_gettop(L) > 0 && !lua_isnil(L, 1) && !lua_istable(L, 1)) {
    return luaL_argerror(L, 1, "table or nil required");
  }
  fm_selection_clear(fm);
  if (lua_istable(L, 1)) {
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      fm_selection_add(fm, luaL_checkstring(L, -1));
    }
  }
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_selection_get(lua_State *L)
{
  lua_createtable(L, fm->selection.paths->size, 0);
  int i = 1;
  lht_foreach(char *path, fm->selection.paths) {
    lua_pushstring(L, path);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int l_fm_selection_reverse(lua_State *L)
{
  (void) L;
  fm_selection_reverse(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_chdir(lua_State *L)
{
  char *path = path_qualify(luaL_optstring(L, 1, "~"), fm->pwd);
  search_nohighlight(lfm);
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE);
  fm_chdir(fm, path, true, true);
  ui_redraw(ui, REDRAW_FM);
  xfree(path);
  return 0;
}

static int l_fm_paste_mode_get(lua_State *L)
{
  lua_pushstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy");
  return 1;
}

static int l_fm_paste_mode_set(lua_State *L)
{
  const char *mode = luaL_checkstring(L, 1);
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "unrecognized paste mode: %s", mode);
  }
  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_fm_paste_buffer_get(lua_State *L)
{
  lua_createtable(L, fm->paste.buffer->size, 0);
  int i = 1;
  lht_foreach(char *path, fm->paste.buffer) {
    lua_pushstring(L, path);
    lua_rawseti(L, -2, i++);
  }
  lua_pushstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy");
  return 2;
}

static int l_fm_paste_buffer_set(lua_State *L)
{
  fm_paste_buffer_clear(fm);

  if (lua_type(L, 1) == LUA_TTABLE) {
    const size_t l = lua_objlen(L, 1);
    for (size_t i = 0; i < l; i++) {
      lua_rawgeti(L, 1, i + 1);
      fm_paste_buffer_add(fm, lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  }

  const char *mode = luaL_optstring(L, 2, "copy");
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "unrecognized paste mode: %s", mode);
  }

  if (luaL_optbool(L, 3, true)) {
    lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  }

  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_fm_copy(lua_State *L)
{
  (void) L;
  fm_paste_mode_set(fm, PASTE_MODE_COPY);
  lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_cut(lua_State *L)
{
  (void) L;
  fm_paste_mode_set(fm, PASTE_MODE_MOVE);
  lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_filter_get(lua_State *L)
{
  lua_pushstring(L, fm_filter_get(fm));
  return 1;
}

static int l_fm_filter(lua_State *L)
{
  fm_filter(fm, luaL_checkstring(L, 1));
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_jump_automark(lua_State *L)
{
  (void) L;
  lfm_run_hook(lfm, LFM_HOOK_CHDIRPRE);
  fm_jump_automark(fm);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_fm_flatten_level(lua_State *L)
{
  lua_pushinteger(L, fm_current_dir(fm)->flatten_level);
  return 1;
}

static int l_fm_flatten(lua_State *L)
{
  int level = luaL_optinteger(L, 1, 0);
  if (level < 0) {
    level = 0;
  }
  fm_flatten(fm, level);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static const struct luaL_Reg fm_lib[] = {
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
  {"sortby", l_fm_sortby},
  {"top", l_fm_top},
  {"visual_start", l_fm_visual_start},
  {"visual_end", l_fm_visual_end},
  {"visual_toggle", l_fm_visual_toggle},
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

int luaopen_fm(lua_State *L)
{
  lua_newtable(L);
  luaL_register(L, NULL, fm_lib);
  return 0;
}
