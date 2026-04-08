#include "cmdline.h"
#include "history.h"
#include "hooks.h"
#include "input.h"
#include "macro.h"
#include "private.h"
#include "ui.h"
#include "util.h"

#include <ev.h>
#include <lauxlib.h>
#include <lua.h>
#include <notcurses/notcurses.h>
#include <stc/cstr.h>

#include <linux/limits.h>

// lua/dir.c
int l_get_dir(lua_State *L);

static int l_set_keymap(lua_State *L) {
  luaL_checktype(L, 2, LUA_TFUNCTION);

  zsview lhs = luaL_checkzsview(L, 1);
  Trie *trie = lfm->ui.maps.normal;
  zsview desc = zsview_init();

  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "desc");
    if (!lua_isnil(L, -1))
      desc = lua_tozsview(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 3, "mode");
    if (!lua_isnil(L, -1)) {
      const hmap_modes_value *mode =
          hmap_modes_get(&lfm->modes, lua_tozsview(L, -1));
      if (mode == NULL)
        return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
      trie = mode->second.maps;
    }
    lua_pop(L, 1);
  }

  int ref = 0;
  if (!lua_isnil(L, 2)) {
    lua_pushvalue(L, 2);
    ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  int oldref;

  int status = input_map(trie, lhs, ref, desc, &oldref);
  if (status < 0) {
    if (ref != 0)
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    if (status == -2) {
      return luaL_error(L, "key sequence too long");
    } else {
      return luaL_error(L, "malformed key sequence");
    }
  }

  if (oldref)
    luaL_unref(L, LUA_REGISTRYINDEX, oldref);

  return 0;
}

static int l_del_keymap(lua_State *L) {
  zsview lhs = luaL_checkzsview(L, 1);
  Trie *trie = lfm->ui.maps.normal;

  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "mode");
    if (!lua_isnil(L, -1)) {
      const hmap_modes_value *mode =
          hmap_modes_get(&lfm->modes, lua_tozsview(L, -1));
      if (mode == NULL)
        return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
      trie = mode->second.maps;
    }
    lua_pop(L, 1);
  }

  int oldref;

  int status = input_map(trie, lhs, 0, zsview_init(), &oldref);
  if (status < 0) {
    if (status == -2)
      return luaL_error(L, "key sequence too long");
    else
      return luaL_error(L, "malformed key sequence");
  }

  if (oldref)
    luaL_unref(L, LUA_REGISTRYINDEX, oldref);

  return 0;
}

static int l_get_keymap(lua_State *L) {
  zsview name = luaL_checkzsview(L, 1);
  const struct mode *mode = hmap_modes_at(&lfm->modes, name);
  if (!mode)
    return luaL_error(L, "no such mode: %s", name);

  bool prune = luaL_optbool(L, 2, false);
  vec_trie maps = trie_collect_leaves(mode->maps, prune);
  lua_createtable(L, vec_trie_size(&maps), 0);
  usize i = 0;
  c_foreach(it, vec_trie, maps) {
    Trie *map = *it.ref;
    lua_createtable(L, 0, 3);
    lua_pushcstr(L, &map->desc);
    lua_setfield(L, -2, "desc");
    lua_pushcstr(L, &map->keys);
    lua_setfield(L, -2, "lhs");
    lua_rawgeti(L, LUA_REGISTRYINDEX, map->ref);
    lua_setfield(L, -2, "rhs");
    lua_rawseti(L, -2, i + 1);
    i++;
  }
  vec_trie_drop(&maps);
  return 1;
}

static int l_cmd_line_get(lua_State *L) {
  lua_pushzsview(L, cmdline_get(&ui->cmdline));
  return 1;
}

static int l_feedkeys(lua_State *L) {
  usize len;
  const char *keys = luaL_checklstring(L, 1, &len);
  for (const char *end = keys + len; keys < end;) {
    input_t u;
    i32 n = key_name_to_input(keys, &u);
    if (n < 0)
      return luaL_error(L, "invalid key");
    input_handle_key(lfm, u);
    keys += n;
  }
  return 0;
}

static int l_input(lua_State *L) {
  usize len;
  const char *keys = luaL_checklstring(L, 1, &len);
  for (const char *end = keys + len; keys < end;) {
    input_t u;
    i32 n = key_name_to_input(keys, &u);
    if (n < 0)
      return luaL_error(L, "invalid key");
    input_buffer_add(lfm, u);
    keys += n;
  }
  return 0;
}

static int l_cmd_line_set(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);

  ui->show_message = false;

  if (cmdline_set(&ui->cmdline, lua_tozsview(L, 1), lua_tozsview(L, 2)))
    ui_redraw(ui, REDRAW_CMDLINE);

  return 0;
}

static int l_cmd_toggle_overwrite(lua_State *L) {
  (void)L;
  if (cmdline_toggle_overwrite(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_clear(lua_State *L) {
  (void)L;
  if (cmdline_clear(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_delete(lua_State *L) {
  (void)L;
  if (cstr_is_empty(&ui->cmdline.left) && cstr_is_empty(&ui->cmdline.right)) {
    lfm_mode_normal(lfm);
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
  if (cmdline_left(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_right(lua_State *L) {
  (void)L;
  if (cmdline_right(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_word_left(lua_State *L) {
  (void)L;
  if (cmdline_word_left(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_word_right(lua_State *L) {
  (void)L;
  if (cmdline_word_right(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
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
  if (cmdline_home(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_end(lua_State *L) {
  (void)L;
  if (cmdline_end(&ui->cmdline))
    ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_cmd_history_append(lua_State *L) {
  history_append(&ui->cmdline.history, luaL_checkzsview(L, 1),
                 luaL_checkzsview(L, 2));
  return 0;
}

static int l_cmd_history_prev(lua_State *L) {
  zsview line = history_prev(&ui->cmdline.history);
  if (zsview_is_empty(line))
    return 0;
  lua_pushzsview(L, line);
  return 1;
}

static int l_cmd_history_next(lua_State *L) {
  zsview line = history_next_entry(&ui->cmdline.history);
  if (zsview_is_empty(line))
    return 0;
  lua_pushzsview(L, line);
  return 1;
}

static int l_cmd_get_history(lua_State *L) {
  int i = history_size(&ui->cmdline.history);
  lua_createtable(L, i, 0);
  c_foreach(it, history, lfm->ui.cmdline.history) {
    lua_pushcstr(L, &it.ref->line);
    lua_rawseti(L, -2, i--);
  }
  return 1;
}

static const struct luaL_Reg cmdline_funcs[] = {
    {"cmdline_clear",            l_cmd_clear           },
    {"cmdline_delete",           l_cmd_delete          },
    {"cmdline_delete_right",     l_cmd_delete_right    },
    {"cmdline_delete_word",      l_cmd_delete_word     },
    {"cmdline__end",             l_cmd_end             },
    {"cmdline_line_get",         l_cmd_line_get        },
    {"cmdline_line_set",         l_cmd_line_set        },
    {"cmdline_home",             l_cmd_home            },
    {"cmdline_insert",           l_cmd_insert          },
    {"cmdline_toggle_overwrite", l_cmd_toggle_overwrite},
    {"cmdline_left",             l_cmd_left            },
    {"cmdline_word_left",        l_cmd_word_left       },
    {"cmdline_word_right",       l_cmd_word_right      },
    {"cmdline_delete_line_left", l_cmd_delete_line_left},
    {"cmdline_right",            l_cmd_right           },
    {"cmdline_history_append",   l_cmd_history_append  },
    {"cmdline_history_next",     l_cmd_history_next    },
    {"cmdline_history_prev",     l_cmd_history_prev    },
    {"cmdline_get_history",      l_cmd_get_history     },
    {NULL,                       NULL                  },
};

struct notcurses **get_notcurses() {
  return &ui->nc;
}

static int l_ui_messages(lua_State *L) {
  lua_createtable(L, vec_message_size(&ui->messages), 0);
  int i = 1;
  c_foreach(it, vec_message, ui->messages) {
    lua_pushcstr(L, &it.ref->text);
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
  i32 delay = luaL_optinteger(L, 2, 0);
  luaL_argcheck(L, delay >= 0, 2, "delay must be non-negative");

  if (lua_type(L, 1) == LUA_TTABLE) {
    lua_read_vec_cstr(L, 1, &menu);
  } else if (lua_type(L, 1) == LUA_TSTRING) {
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
  if (luaL_optbool(L, 1, false))
    ui_redraw(ui, REDRAW_FULL);
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
  if (macro_recording) {
    usize len;
    const char *str = input_to_key_name(macro_identifier, &len);
    lua_pushlstring(L, str, len);
    return 1;
  }
  return 0;
}

static int l_macro_record(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  input_t in;
  if (key_name_to_input(str, &in) <= 0)
    return luaL_error(L, "converting to input_t");
  if (macro_record(in))
    return luaL_error(L, "already recording");
  return 0;
}

static int l_macro_stop_record(lua_State *L) {
  if (macro_stop_record())
    return luaL_error(L, "currently not recording");
  return 0;
}

static int l_macro_play(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  input_t in;
  if (key_name_to_input(str, &in) <= 0)
    return luaL_error(L, "converting to input_t");
  if (macro_play(in, lfm))
    return luaL_error(L, "no such macro");
  return 0;
}

static int l_get_tags(lua_State *L) {
  LUA_CHECK_ARGC(L, 1);
  luaL_checktype(L, 1, LUA_TSTRING);
  zsview path = lua_tozsview(L, 1);
  map_zsview_dir_value *v =
      map_zsview_dir_get_mut(&lfm->loader.dir_cache, path);
  lua_newtable(L);
  if (v == NULL)
    return 1;

  Dir *dir = v->second;
  c_foreach_kv(k, v, hmap_cstr, dir->tags.tags) {
    lua_pushcstr(L, v);
    lua_setfield(L, -2, cstr_str(k));
  }
  lua_pushnumber(L, dir->tags.cols);
  return 2;
}

static int l_set_tags(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 3);

  luaL_checktype(L, 1, LUA_TSTRING);
  zsview path = lua_tozsview(L, 1);

  map_zsview_dir_value *v =
      map_zsview_dir_get_mut(&lfm->loader.dir_cache, path);
  if (v == NULL) {
    // not loaded
    lua_pushboolean(L, false);
    return 1;
  }
  Dir *dir = v->second;

  if (lua_isnil(L, 2)) {
    hmap_cstr_clear(&dir->tags.tags);
    dir->tags.cols = 0;
    lua_pushboolean(L, true);
    ui_redraw(ui, REDRAW_FM);
    lua_pushboolean(L, true);
    return 1;
  }
  luaL_checktype(L, 2, LUA_TTABLE);

  int cols = -1;
  if (lua_gettop(L) == 3) {
    cols = luaL_checkinteger(L, 3);
    lua_pop(L, 1);
  }

  hmap_cstr *tags = &dir->tags.tags;
  hmap_cstr_clear(tags);
  for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
    hmap_cstr_insert(tags, lua_tocstr(L, -2), lua_tocstr(L, -1));
  }
  lua_pop(L, 1);

  if (cols > -1)
    dir->tags.cols = cols;

  ui_redraw(ui, REDRAW_FULL);

  lua_pushboolean(L, true);
  return 1;
}

static const struct luaL_Reg ui_funcs[] = {
    {"set_keymap",         l_set_keymap       },
    {"del_keymap",         l_del_keymap       },
    {"get_keymap",         l_get_keymap       },
    {"input",              l_input            },
    {"feedkeys",           l_feedkeys         },
    {"set_directory_tags", l_set_tags         },
    {"get_directory_tags", l_get_tags         },
    {"macro_recording",    l_macro_recording  },
    {"macro_record",       l_macro_record     },
    {"macro_stop_record",  l_macro_stop_record},
    {"macro_play",         l_macro_play       },
    {"ui_get_width",       l_ui_get_width     },
    {"ui_get_height",      l_ui_get_height    },
    {"ui_clear",           l_ui_clear         },
    {"redraw",             l_ui_redraw        },
    {"ui_menu",            l_ui_menu          },
    {"get_messages",       l_ui_messages      },
    {NULL,                 NULL               },
};

static const struct luaL_Reg nc_funcs[] = {
    {"notcurses_palette_size",   l_notcurses_palette_size  },
    {"notcurses_cantruecolor",   l_notcurses_cantruecolor  },
    {"notcurses_canopen_images", l_notcurses_canopen_images},
    {"notcurses_canhalfblock",   l_notcurses_canhalfblock  },
    {"notcurses_canquadrant",    l_notcurses_canquadrant   },
    {"notcurses_cansextant",     l_notcurses_cansextant    },
    {"notcurses_canbraille",     l_notcurses_canbraille    },
    {"notcurses_canpixel",       l_notcurses_canpixel      },
    {NULL,                       NULL                      },
};

static int l_current_mode(lua_State *L) {
  lua_pushcstr(L, &lfm->current_mode->name);
  return 1;
}

static int l_get_modes(lua_State *L) {
  lua_createtable(L, lfm->modes.size, 0);
  int i = 1;
  c_foreach_kv(_, v, hmap_modes, lfm->modes) {
    lua_pushcstr(L, &v->name);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int l_mode(lua_State *L) {
  if (lfm_mode_enter(lfm, luaL_checkzsview(L, 1)) != 0)
    return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
  return 0;
}

static int l_create_mode(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  struct mode mode = {
      .type = MODE_LUA,
  };

  lua_getfield(L, 1, "name");
  if (lua_isnil(L, -1))
    return luaL_error(L, "register_mode: missing field 'name'");

  zsview name = lua_tozsview(L, -1);
  if (lfm_mode_exists(lfm, name))
    return luaL_error(L, "register_mode: mode '%s' already exists", name.str);
  mode.name = cstr_from_zv(name);
  lua_pop(L, 1);

  lua_getfield(L, 1, "input");
  mode.is_input = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "prefix");
  mode.prefix = cstr_from_zv(lua_tozsview(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_enter");
  if (!lua_isnil(L, -1))
    mode.on_enter_ref = lua_register_callback(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_change");
  if (!lua_isnil(L, -1))
    mode.on_change_ref = lua_register_callback(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_return");
  if (!lua_isnil(L, -1))
    mode.on_return_ref = lua_register_callback(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_esc");
  if (!lua_isnil(L, -1))
    mode.on_esc_ref = lua_register_callback(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_exit");
  if (!lua_isnil(L, -1))
    mode.on_exit_ref = lua_register_callback(L, -1);
  lua_pop(L, 1);

  lfm_mode_register(lfm, &mode);

  return 0;
}

static int l_update_mode(lua_State *L) {
  zsview name = luaL_checkzsview(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);

  if (!lfm_mode_exists(lfm, name))
    return luaL_error(L, "no such mode '%s'", name.str);
  struct mode *mode = hmap_modes_at_mut(&lfm->modes, name);

  lua_getfield(L, 2, "prefix");
  if (!lua_isnil(L, -1))
    cstr_assign_zv(&mode->prefix, lua_tozsview(L, -1));
  lua_pop(L, 1);

  if (mode->type == MODE_LUA) {

    lua_getfield(L, 2, "on_enter");
    if (!lua_isnil(L, -1))
      lua_replace_callback(L, -1, &mode->on_enter_ref);
    lua_pop(L, 1);

    lua_getfield(L, 2, "on_change");
    if (!lua_isnil(L, -1))
      lua_replace_callback(L, -1, &mode->on_change_ref);
    lua_pop(L, 1);

    lua_getfield(L, 2, "on_return");
    if (!lua_isnil(L, -1))
      lua_replace_callback(L, -1, &mode->on_return_ref);
    lua_pop(L, 1);

    lua_getfield(L, 2, "on_esc");
    if (!lua_isnil(L, -1))
      lua_replace_callback(L, -1, &mode->on_esc_ref);
    lua_pop(L, 1);

    lua_getfield(L, 2, "on_exit");
    if (!lua_isnil(L, -1))
      lua_replace_callback(L, -1, &mode->on_exit_ref);
    lua_pop(L, 1);
  }

  return 0;
}

int l_add_hook(lua_State *L) {
  LUA_CHECK_ARGC(L, 2);
  const char *name = luaL_checkstring(L, 1);
  int id = hook_name_to_id(name);
  if (id == -1)
    return luaL_error(L, "no such hook: %s", name);
  id = lfm_add_hook(lfm, id, lua_register_callback(L, 2));
  lua_pushnumber(L, id);
  return 1;
}

int l_del_hook(lua_State *L) {
  int id = luaL_checknumber(L, 1);
  int ref = lfm_remove_hook(lfm, id);
  if (!ref)
    return luaL_error(L, "no hook with id %d", id);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return 0;
}

static const struct luaL_Reg api_funcs[] = {
    {"get_dir",      l_get_dir     },
    {"add_hook",     l_add_hook    },
    {"del_hook",     l_del_hook    },
    {"mode",         l_mode        },
    {"current_mode", l_current_mode},
    {"get_modes",    l_get_modes   },
    {"create_mode",  l_create_mode },
    {"update_mode",  l_update_mode },
    {NULL,           NULL          },
};

int luaopen_api(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, cmdline_funcs);
  luaL_register(L, NULL, ui_funcs);
  luaL_register(L, NULL, nc_funcs);
  luaL_register(L, NULL, api_funcs);
  return 1;
}
