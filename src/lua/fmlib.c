#include "config.h"
#include "fm.h"
#include "hooks.h"
#include "macro.h"
#include "path.h"
#include "search.h"
#include "selection.h"
#include "stc/cstr.h"
#include "ui.h"
#include "visual.h"

#include "private.h"
#include "util.h"

#include <ev.h>
#include <lauxlib.h>
#include <locale.h>
#include <lua.h>

#include <linux/limits.h>
#include <notcurses/notcurses.h>
#include <stdint.h>

static int l_get_height(lua_State *L) {
  lua_pushnumber(L, fm->height);
  return 1;
}

static int l_drop_cache(lua_State *L) {
  (void)L;
  fm_drop_cache(fm);
  ui_drop_cache(ui);
  return 0;
}

static int l_reload(lua_State *L) {
  (void)L;
  fm_reload(fm);
  return 0;
}

static int l_check(lua_State *L) {
  (void)L;
  Dir *dir = fm_current_dir(fm);
  async_dir_check(&lfm->async, dir);
  return 0;
}

static int l_load(lua_State *L) {
  char buf[PATH_MAX + 1];
  zsview path = luaL_checkzsview(L, 1);
  zsview normalized = path_normalize3(path, fm_getpwd_str(fm), buf, sizeof buf);
  if (zsview_is_empty(normalized))
    return luaL_error(L, "path too long");
  loader_dir_from_path(&lfm->loader, normalized, true);
  return 0;
}

static int l_select(lua_State *L) {
  Dir *dir = fm_current_dir(fm);
  zsview name = luaL_checkzsview(L, 1);
  dir_move_cursor_to_name(dir, name, fm->height, cfg.scrolloff);
  update_preview(true);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_up(lua_State *L) {
  i32 ct = luaL_optinteger(L, 1, 1);
  Dir *dir = fm_current_dir(fm);
  u32 ind = dir->ind;
  if (dir_move_cursor(dir, -ct, fm->height, cfg.scrolloff)) {
    update_preview(false);
    visual_update_selection(fm, ind, dir->ind);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_down(lua_State *L) {
  i32 ct = luaL_optinteger(L, 1, 1);
  Dir *dir = fm_current_dir(fm);
  u32 ind = dir->ind;
  if (dir_move_cursor(dir, ct, fm->height, cfg.scrolloff)) {
    update_preview(false);
    visual_update_selection(fm, ind, dir->ind);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_top(lua_State *L) {
  (void)L;
  Dir *dir = fm_current_dir(fm);
  u32 ind = dir->ind;
  if (dir_set_cursor(dir, 0, fm->height, cfg.scrolloff)) {
    update_preview(true);
    visual_update_selection(fm, ind, dir->ind);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_bot(lua_State *L) {
  (void)L;
  Dir *dir = fm_current_dir(fm);
  u32 ind = dir->ind;
  if (dir_set_cursor(dir, dir_length(dir) /*-1*/, fm->height, cfg.scrolloff)) {
    update_preview(true);
    visual_update_selection(fm, ind, dir->ind);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_scroll_up(lua_State *L) {
  (void)L;
  Dir *dir = fm_current_dir(fm);
  if (dir_scroll_up(dir, fm->height, cfg.scrolloff)) {
    update_preview(true);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_scroll_down(lua_State *L) {
  (void)L;
  Dir *dir = fm_current_dir(fm);
  if (dir_scroll_down(dir, fm->height, cfg.scrolloff)) {
    update_preview(true);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_updir(lua_State *L) {
  (void)L;
  if (fm_updir(fm)) {
    // I don't remember why we run th chdir post hook here,
    // since we are also not running the pre hook
    // LFM_RUN_HOOK(lfm, LFM_HOOK_CHDIRPOST, &fm->pwd);
    search_nohighlight(lfm);
    ui_update_preview(ui, true);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_open(lua_State *L) {
  lfm_mode_exit(lfm, c_zv("visual"));
  File *file = fm_open(fm);
  if (file) {
    if (lfm->opts.selection_path) {
      selection_write(&lfm->fm, zsview_from(lfm->opts.selection_path));
      return lua_quit(L, lfm);
    }

    lua_pushzsview(L, file_path(file));
    return 1;
  } else {
    /* changed directory */
    // LFM_RUN_HOOK(lfm, LFM_HOOK_CHDIRPOST, &fm->pwd);
    ui_update_preview(ui, true);
    ui_redraw(ui, REDRAW_FM);
    search_nohighlight(lfm);
    return 0;
  }
}

static int l_current_file(lua_State *L) {
  File *file = fm_current_file(fm);
  if (!file)
    return 0;
  lua_pushzsview(L, file_path(file));
  return 1;
}

static int l_push_files(lua_State *L) {
  zsview path = lua_tozsview(L, lua_upvalueindex(1));
  Dir *dir = loader_dir_from_path(&lfm->loader, path, false);
  if (dir == NULL)
    return luaL_error(L, "no such directory: %s", path.str);

  lua_createtable(L, dir_length(dir), 0);
  usize i = 1;
  c_foreach(it, Dir, dir) {
    lua_pushzsview(L, file_name(*it.ref));
    lua_rawseti(L, -2, i++);
  }

  return 1;
}

static int l_current_dir(lua_State *L) {
  const Dir *dir = fm_current_dir(fm);
  lua_createtable(L, 0, 3);

  lua_pushzsview(L, dir_path(dir));
  lua_setfield(L, -2, "path");

  lua_pushzsview(L, dir_name(dir));
  lua_setfield(L, -2, "name");

  lua_createtable(L, 0, 3);
  lua_pushstring(L, sorttype_str[dir->settings.sorttype]);
  lua_setfield(L, -2, "type");
  lua_pushboolean(L, dir->settings.dirfirst);
  lua_setfield(L, -2, "dirfirst");
  lua_pushboolean(L, dir->settings.reverse);
  lua_setfield(L, -2, "reverse");
  lua_setfield(L, -2, "sortopts");

  lua_pushzsview(L, dir_path(dir));
  lua_pushcclosure(L, l_push_files, 1);
  lua_setfield(L, -2, "files");

  return 1;
}

static int l_get_info(lua_State *L) {
  Dir *dir = fm_current_dir(fm);
  lua_pushstring(L, fileinfo_str[dir->settings.fileinfo]);
  return 1;
}

static int l_set_info(lua_State *L) {
  const char *val = luaL_checkstring(L, 1);
  int info = fileinfo_from_str(val);
  if (info < 0)
    return luaL_error(L, "invalid option for info: %s", val);
  Dir *dir = fm_current_dir(fm);
  dir->settings.fileinfo = info;
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_sort(lua_State *L) {
  lfm_mode_exit(lfm, c_zv("visual"));
  luaL_checktype(L, 1, LUA_TTABLE);
  Dir *dir = fm_current_dir(fm);

  struct dir_settings settings = dir->settings;
  lua_getfield(L, 1, "dirfirst");
  if (!lua_isnil(L, -1))
    settings.dirfirst = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "reverse");
  if (!lua_isnil(L, -1))
    settings.reverse = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "type");
  if (!lua_isnil(L, -1)) {
    const char *op = luaL_checkstring(L, -1);
    int type = sorttype_from_str(op);
    if (type < 0)
      return luaL_error(L, "unrecognized sort type: %s", op);
    settings.sorttype = type;
  }
  lua_pop(L, 1);

  dir->settings = settings;

  // it is convenient to keep the cursor position in a (fresh)
  // directory if it hasn't been moved when sorting
  const File *file = dir_current_file(dir);
  dir_sort(dir, true);
  dir_move_cursor_to_ptr(dir, file, fm->height, cfg.scrolloff);
  ui_update_preview(ui, true);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_toggle_selection(lua_State *L) {
  (void)L;
  File *file = fm_current_file(fm);
  if (file) {
    selection_toggle_path(fm, file_path(file), true);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_add_selection(lua_State *L) {
  char buf[PATH_MAX];
  luaL_checktype(L, 1, LUA_TTABLE);
  int n = lua_objlen(L, 1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    zsview path = lua_tozsview(L, -1);
    zsview normalized =
        path_normalize3(path, fm_getpwd_str(fm), buf, sizeof buf);
    if (zsview_is_empty(normalized))
      return luaL_error(L, "path too long");
    selection_add_path(fm, zsview_from(buf), false);
    lua_pop(L, 1);
  }
  if (n > 0) {
    LFM_RUN_HOOK(lfm, LFM_HOOK_SELECTION);
    ui_redraw(ui, REDRAW_FM);
  }
  return 0;
}

static int l_set_selection(lua_State *L) {
  if (lua_gettop(L) > 0 && !lua_isnil(L, 1) && !lua_istable(L, 1)) {
    return luaL_argerror(L, 1, "table or nil required");
  }
  char buf[PATH_MAX];
  selection_clear(fm);
  lfm_mode_exit(lfm, c_zv("visual"));
  if (lua_istable(L, 1)) {
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      zsview str = lua_tozsview(L, -1);
      zsview normalized =
          path_normalize3(str, fm_getpwd_str(fm), buf, sizeof buf);
      if (zsview_is_empty(normalized))
        return luaL_error(L, "path too long");
      selection_add_path(fm, zsview_from(buf), false);
    }
  }
  LFM_RUN_HOOK(lfm, LFM_HOOK_SELECTION);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_get_selection(lua_State *L) {
  lua_createtable(L, pathlist_size(&fm->selection.current), 0);
  int i = 1;
  c_foreach(it, pathlist, fm->selection.current) {
    lua_pushcstr(L, it.ref);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int l_reverse_selection(lua_State *L) {
  (void)L;
  selection_reverse(fm, fm_current_dir(fm));
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_restore_selection(lua_State *L) {
  (void)L;
  pathlist tmp = fm->selection.current;
  fm->selection.current = fm->selection.previous;
  fm->selection.previous = tmp;
  LFM_RUN_HOOK(lfm, LFM_HOOK_SELECTION);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_chdir(lua_State *L) {
  char buf[PATH_MAX + 1];
  zsview arg = luaL_optzsview(L, 1, c_zv("~"));
  bool force_sync = luaL_optbool(L, 2, false);

  // TODO: can't remember why we are doing this, also this actually finds the
  // first slash, not the last
  const char *last_slash = strchr(arg.str, '/');
  bool should_save = (arg.str[0] == '/' || arg.str[0] == '~' ||
                      (last_slash != NULL && last_slash[1] != 0));

  zsview path = path_normalize3(arg, fm_getpwd_str(fm), buf, sizeof buf);
  if (zsview_is_empty(path))
    return luaL_error(L, "path too long");

  search_nohighlight(lfm);
  lfm_mode_exit(lfm, c_zv("visual"));
  LFM_RUN_HOOK(lfm, LFM_HOOK_CHDIRPRE, &fm->pwd);
  if (force_sync || macro_playing) {
    fm_sync_chdir(fm, path, should_save, true);
  } else {
    fm_async_chdir(fm, path, should_save, true);
  }
  ui_update_preview(ui, true);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_set_paste_mode(lua_State *L) {
  lua_pushlstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy", 4);
  return 1;
}

static int l_get_paste_mode(lua_State *L) {
  const char *mode = luaL_checkstring(L, 1);
  paste_mode prev = fm->paste.mode;
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "invalid paste mode: %s", mode);
  }
  if (fm->paste.mode != prev)
    LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_get_paste_buffer(lua_State *L) {
  lua_createtable(L, pathlist_size(&fm->paste.buffer), 0);
  int i = 1;
  c_foreach(it, pathlist, fm->paste.buffer) {
    lua_pushcstr(L, it.ref);
    lua_rawseti(L, -2, i++);
  }
  lua_pushlstring(L, fm->paste.mode == PASTE_MODE_MOVE ? "move" : "copy", 4);
  return 2;
}

static int l_set_paste_buffer(lua_State *L) {
  usize prev_size = pathlist_size(&fm->paste.buffer);
  paste_mode prev_mode = fm->paste.mode;
  paste_buffer_clear(fm);

  const char *mode = luaL_optstring(L, 2, "copy");
  if (streq(mode, "copy")) {
    fm->paste.mode = PASTE_MODE_COPY;
  } else if (streq(mode, "move")) {
    fm->paste.mode = PASTE_MODE_MOVE;
  } else {
    return luaL_error(L, "unrecognized paste mode: %s", mode);
  }

  if (lua_type(L, 1) == LUA_TTABLE) {
    const usize l = lua_objlen(L, 1);
    for (usize i = 0; i < l; i++) {
      lua_rawgeti(L, 1, i + 1);
      zsview path = lua_tozsview(L, -1);
      paste_buffer_add(fm, path);
      lua_pop(L, 1);
    }
  }

  if (luaL_optbool(L, 3, true) &&
      (pathlist_size(&fm->paste.buffer) != prev_size ||
       fm->paste.mode != prev_mode)) {
    LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
  }

  ui_redraw(ui, REDRAW_FM);

  return 0;
}

static int l_copy(lua_State *L) {
  (void)L;
  lfm_mode_exit(lfm, c_zv("visual"));
  paste_mode_set(fm, PASTE_MODE_COPY);
  LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_cut(lua_State *L) {
  (void)L;
  lfm_mode_exit(lfm, c_zv("visual"));
  paste_mode_set(fm, PASTE_MODE_MOVE);
  LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_get_filter(lua_State *L) {
  Filter *filter = fm_current_dir(fm)->filter;
  if (!filter)
    return 0;
  lua_pushzsview(L, filter_string(filter));
  lua_pushzsview(L, filter_type(filter));
  return 2;
}

static int l_filter(lua_State *L) {
  Dir *dir = fm_current_dir(fm);
  Filter *filter = NULL;
  if (!lua_isnoneornil(L, 1)) {
    const char *type = lua_tostring(L, 2);
    if (!type || streq(type, "substring")) {
      filter = filter_create_sub(lua_tozsview(L, 1));
    } else if (streq(type, "fuzzy")) {
      filter = filter_create_fuzzy(lua_tozsview(L, 1));
    } else if (streq(type, "lua")) {
      int ref = lua_register_callback(L, 1);
      filter = filter_create_lua(ref, L);
    } else {
      return luaL_error(L, "unrecognized filter type: %s", type);
    }
  }
  dir_filter(dir, filter, fm->height, cfg.scrolloff);
  update_preview(false);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_get_automark(lua_State *L) {
  if (cstr_is_empty(&fm->automark))
    return 0;
  lua_pushcstr(L, &fm->automark);
  return 1;
}

static int l_jump_automark(lua_State *L) {
  (void)L;
  LFM_RUN_HOOK(lfm, LFM_HOOK_CHDIRPRE, &fm->pwd);
  lfm_mode_exit(lfm, c_zv("visual"));
  fm_jump_automark(fm);
  ui_update_preview(ui, true);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_get_flatten_level(lua_State *L) {
  lua_pushinteger(L, fm_current_dir(fm)->flatten_level);
  return 1;
}

static int l_set_flatten_level(lua_State *L) {
  int level = luaL_optinteger(L, 1, 0);
  if (level < 0)
    level = 0;

  /* TODO: To reload flattened directories properly, more inotify watchers are
   * needed (on 2022-02-06) */
  Dir *dir = fm_current_dir(fm);
  dir->flatten_level = level;
  async_dir_load(&to_lfm(fm)->async, dir, level == 0);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_get_cached_dirs(lua_State *L) {
  usize n = dircache_size(&lfm->loader.dc);
  lua_createtable(L, n, 0);
  int i = 1;
  c_foreach(it, dircache, lfm->loader.dc) {
    lua_pushzsview(L, dir_path(it.ref->second));
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static const struct luaL_Reg fm_funcs[] = {
    {"chdir",             l_chdir            },
    {"up",                l_up               },
    {"down",              l_down             },
    {"top",               l_top              },
    {"bottom",            l_bot              },
    {"updir",             l_updir            },
    {"open",              l_open             },
    {"scroll_down",       l_scroll_down      },
    {"scroll_up",         l_scroll_up        },
    {"sort",              l_sort             },
    {"select",            l_select           },
    {"set_info",          l_set_info         },
    {"get_info",          l_get_info         },
    {"set_flatten_level", l_set_flatten_level},
    {"get_flatten_level", l_get_flatten_level},
    {"set_filter",        l_filter           },
    {"get_filter",        l_get_filter       },
    {"get_automark",      l_get_automark     },
    {"jump_automark",     l_jump_automark    },
    {"current_dir",       l_current_dir      },
    {"current_file",      l_current_file     },
    {"get_selection",     l_get_selection    },
    {"set_selection",     l_set_selection    },
    {"add_selection",     l_add_selection    },
    {"toggle_selection",  l_toggle_selection },
    {"reverse_selection", l_reverse_selection},
    {"restore_selection", l_restore_selection},
    {"get_paste_buffer",  l_get_paste_buffer },
    {"set_paste_buffer",  l_set_paste_buffer },
    {"get_paste_mode",    l_set_paste_mode   },
    {"set_paste_mode",    l_get_paste_mode   },
    {"cut",               l_cut              },
    {"copy",              l_copy             },
    {"check",             l_check            },
    {"load",              l_load             },
    {"drop_cache",        l_drop_cache       },
    {"reload",            l_reload           },
    {"get_height",        l_get_height       },
    {"get_cached_dirs",   l_get_cached_dirs  },
    {NULL,                NULL               },
};

int luaopen_fm(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, fm_funcs);
  return 1;
}
