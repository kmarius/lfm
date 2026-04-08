#include "dir.h"
#include "config.h"
#include "private.h"
#include "util.h"
#include "visual.h"

#include <lauxlib.h>
#include <lua.h>
#include <stc/cstr.h>

#define DIR_META "Lfm.Dir.Meta"
#define DIR_METHODS "Lfm.Dir.Methods"

static inline void pushdir(lua_State *L, Dir *dir);
static inline void pushdir_from_path(lua_State *L, zsview path);
static inline Dir *checkdir(lua_State *L, int idx) {
  return *(Dir **)luaL_checkudata(L, idx, DIR_META);
}

static inline void move_cursor(Dir *dir, i32 ct) {
  u32 cur = dir->ind;
  dir_move_cursor(dir, ct, fm->height, cfg.scrolloff);
  if (dir->ind != cur) {
    if (dir == fm_current_dir(fm)) {
      visual_update_selection(fm, cur, dir->ind);
      update_preview(false);
    }
    if (dir->visible)
      ui_redraw(ui, REDRAW_FM);
  }
}

static inline void move_cursor_to_name(Dir *dir, zsview name) {
  dir_move_cursor_to_name(dir, name, fm->height, cfg.scrolloff);
  if (dir == fm_current_dir(fm))
    update_preview(false);
  if (dir->visible)
    ui_redraw(ui, REDRAW_FM);
}

static inline void restore_cursor(Dir *dir, File *file) {
  if (file) {
    move_cursor_to_name(dir, file_name(file));
  }
}

/* methods */

static int l_dir_up(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  move_cursor(dir, -luaL_optint(L, 2, 1));
  return 0;
}

static int l_dir_down(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  move_cursor(dir, luaL_optint(L, 2, 1));
  return 0;
}

static int l_dir_select(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  zsview name = luaL_checkzsview(L, 2);
  move_cursor_to_name(dir, name);
  return 0;
}

// sort the given directory, options table at idx
static inline int sort_dir(lua_State *L, int idx, Dir *dir) {
  luaL_checktype(L, idx, LUA_TTABLE);

  struct dir_settings settings = dir->settings;
  lua_getfield(L, idx, "dirfirst");
  if (!lua_isnil(L, -1))
    settings.dirfirst = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "reverse");
  if (!lua_isnil(L, -1))
    settings.reverse = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "type");
  if (!lua_isnil(L, -1)) {
    const char *op = luaL_checkstring(L, -1);
    int type = sorttype_from_str(op);
    if (type < 0)
      return luaL_error(L, "unrecognized sort type: %s", op);
    settings.sorttype = type;
  }
  lua_pop(L, 1);

  bool have_keyfunc = false;
  lua_getfield(L, idx, "keyfunc");
  if (!lua_isnil(L, -1)) {
    luaL_checktype(L, -1, LUA_TFUNCTION);
    lfm_lua_store_keyfunc(lfm, -1, dir_path(dir));
    settings.sorttype = SORT_LUA;
    have_keyfunc = true;
  }
  lua_pop(L, 1);

  if (settings.sorttype == SORT_LUA && !have_keyfunc)
    return luaL_error(L, "missing field: keyfunc");

  dir->settings = settings;

  if (settings.sorttype == SORT_LUA)
    lfm_lua_apply_keyfunc(lfm, dir, true);

  // sort and restore cursor
  File *file = dir_current_file(dir);
  dir_sort(dir, true);
  restore_cursor(dir, file);

  return 0;
}

// lfm.fm.sort
int l_fm_sort(lua_State *L) {
  lfm_mode_exit(lfm, c_zv("visual"));
  Dir *dir = fm_current_dir(fm);
  sort_dir(L, 1, dir);

  ui_update_preview(ui, true);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

// method
static int l_dir_sort(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  sort_dir(L, 2, dir);

  if (dir == fm_current_dir(fm))
    update_preview(false);
  if (dir->visible)
    ui_redraw(ui, REDRAW_FM);

  return 0;
}

// not sure if we want to keep the filter definition as a table,
// it is often not convenient
static inline int filter(lua_State *L, int idx, Dir *dir) {
  Filter *filter = NULL;
  if (lua_type(L, idx) == LUA_TSTRING) {
    filter = filter_create_sub(lua_tozsview(L, idx));
  } else if (lua_type(L, idx) == LUA_TTABLE) {
    lua_getfield(L, idx, "type");
    const char *type = luaL_checkstring(L, -1);

    if (streq(type, "substring")) {
      lua_getfield(L, idx, "string");
      filter = filter_create_sub(lua_tozsview(L, -1));
    } else if (streq(type, "fuzzy")) {
      lua_getfield(L, idx, "string");
      filter = filter_create_fuzzy(lua_tozsview(L, -1));
    } else if (streq(type, "lua")) {
      lua_getfield(L, idx, "match");
      int ref = lua_register_callback(L, -1);
      filter = filter_create_lua(ref, L);
    } else {
      return luaL_error(L, "unrecognized filter type: %s", type);
    }
    lua_pop(L, 2);
  }

  dir_filter(dir, filter, fm->height, cfg.scrolloff);

  if (dir == fm_current_dir(fm))
    update_preview(false);
  if (dir->visible)
    ui_redraw(ui, REDRAW_FM);

  return 0;
}

/* metamethods */

static int l_dir__index(lua_State *L) {
  const char *field = luaL_checkstring(L, 2);

  // check if the field is a method
  lua_getfield(L, LUA_REGISTRYINDEX, DIR_METHODS);
  lua_getfield(L, -1, field);
  if (!lua_isnil(L, -1))
    return 1;
  lua_pop(L, 2);

  Dir *dir = checkdir(L, 1);
  if (streq(field, "path")) {
    lua_pushzsview(L, dir_path(dir));
  } else if (streq(field, "name")) {
    lua_pushzsview(L, dir_name(dir));
  } else if (streq(field, "parent")) {
    if (dir_is_root(dir))
      return 0;
    zsview path = path_parent_zv(dir_path(dir));
    pushdir_from_path(L, path);
  } else if (streq(field, "size")) {
    // TODO: how do we want to show visible/hidden/filtered?
    lua_pushinteger(L, dir_length(dir));
  } else if (streq(field, "index")) {
    lua_pushinteger(L, dir->ind + 1);
  } else if (streq(field, "files")) {
    lua_createtable(L, dir_length(dir), 0);
    usize i = 1;
    c_foreach(it, Dir, dir) {
      lua_pushzsview(L, file_name(*it.ref));
      lua_rawseti(L, -2, i++);
    }
  } else if (streq(field, "current_file")) {
    File *file = dir_current_file(dir);
    if (!file)
      return 0;
    lua_pushzsview(L, file_name(file));
  } else if (streq(field, "filter")) {
    Dir *dir = checkdir(L, 1);
    Filter *filter = dir->filter;
    if (!filter)
      return 0;
    lua_newtable(L);
    lua_pushzsview(L, filter_string(filter));
    lua_setfield(L, -2, "string");
    lua_pushzsview(L, filter_type(filter));
    lua_setfield(L, -2, "type");
  } else {
    return luaL_error(L, "invalid field: %s", field);
  }
  return 1;
}

static int l_dir__newindex(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  const char *field = luaL_checkstring(L, 2);
  if (streq(field, "index")) {
    i32 ind = luaL_checkinteger(L, 3) - 1;
    i64 cur = dir->ind;
    move_cursor(dir, ind - cur);
  } else if (streq(field, "filter")) {
    filter(L, 3, dir);
  } else {
    return luaL_error(L, "invalid field: %s", field);
  }
  return 1;
}

static int l_dir__gc(lua_State *L) {
  Dir *dir = checkdir(L, 1);
  dir_dec_ref(dir);
  return 0;
}

static const struct luaL_Reg dir_methods[] = {
    {"up",     l_dir_up    },
    {"down",   l_dir_down  },
    {"select", l_dir_select},
    {"sort",   l_dir_sort  },
    {NULL,     NULL        }
};

static const struct luaL_Reg dir_metamethods[] = {
    {"__gc",       l_dir__gc      },
    {"__index",    l_dir__index   },
    {"__newindex", l_dir__newindex},
    {NULL,         NULL           }
};

static inline void pushdir(lua_State *L, Dir *dir) {
  Dir **ud = lua_newuserdata(L, sizeof *ud);
  *ud = dir_inc_ref(dir);

  if (luaL_newmetatable(L, DIR_META)) {
    luaL_register(L, NULL, dir_metamethods);

    lua_pushliteral(L, DIR_METHODS);
    luaL_newlib(L, dir_methods);
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  lua_setmetatable(L, -2);
}

static inline void pushdir_from_path(lua_State *L, zsview path) {
  Dir *dir = loader_dir_from_path(&lfm->loader, path, true);
  pushdir(L, dir);
}

// registered in apilib.c
int l_get_dir(lua_State *L) {
  if (lua_isnoneornil(L, 1)) {
    pushdir(L, fm_current_dir(fm));
  } else {
    pushdir_from_path(L, luaL_checkzsview(L, 1));
  }
  return 1;
}
