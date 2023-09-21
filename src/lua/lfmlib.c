#include <errno.h>
#include <lauxlib.h>
#include <stdbool.h>
#include <stdlib.h>

#include "auto/versiondef.h"
#include "cmd.h"
#include "config.h"
#include "fm.h"
#include "fn.h"
#include "internal.h"
#include "lfmlua.h"
#include "log.h"
#include "lua.h"
#include "rifle.h"
#include "ui.h"

#include "../config.h"
#include "../find.h"
#include "../hooks.h"
#include "../input.h"
#include "../log.h"
#include "../search.h"

#define LFM_MODES_META "lfm_modes_mt"
#define LFM_MODE_META "lfm_mode_mt"

static int l_schedule(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int delay = 0;
  if (lua_gettop(L) >= 2) {
    delay = luaL_checknumber(L, 2);
  }
  if (delay < 0) {
    delay = 0;
  }
  lua_pushvalue(L, 1);
  lfm_schedule(lfm, lua_set_callback(L), delay);
  return 0;
}

static int l_colors_clear(lua_State *L) {
  (void)L;
  config_colors_clear();
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_handle_key(lua_State *L) {
  const char *keys = luaL_checkstring(L, 1);
  input_t *buf = xmalloc((strlen(keys) + 1) * sizeof *buf);
  key_names_to_input(keys, buf);
  for (input_t *u = buf; *u; u++) {
    lfm_handle_key(lfm, *u);
  }
  xfree(buf);
  return 0;
}

static int l_search(lua_State *L) {
  search(lfm, luaL_optstring(L, 1, NULL), true);
  return 0;
}

static int l_search_backwards(lua_State *L) {
  search(lfm, luaL_optstring(L, 1, NULL), false);
  return 0;
}

static int l_nohighlight(lua_State *L) {
  (void)L;
  search_nohighlight(lfm);
  return 0;
}

static int l_search_next(lua_State *L) {
  search_next(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_search_prev(lua_State *L) {
  search_prev(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_find(lua_State *L) {
  lua_pushboolean(L, find(lfm, luaL_checkstring(L, 1)));
  return 1;
}

static int l_find_clear(lua_State *L) {
  (void)L;
  find_clear(lfm);
  return 0;
}

static int l_find_next(lua_State *L) {
  (void)L;
  find_next(lfm);
  return 0;
}

static int l_find_prev(lua_State *L) {
  (void)L;
  find_prev(lfm);
  return 0;
}

static int l_crash(lua_State *L) {
  xfree(L);
  return 0;
}

static int l_quit(lua_State *L) {
  return lua_quit(L, lfm);
}

static int l_print(lua_State *L) {
  int n = lua_gettop(L);
  lua_getglobal(L, "tostring");
  char *buf = xcalloc(128, 1);
  size_t buflen = 128;
  size_t ind = 0;
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, -1);
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    const char *s = lua_tostring(L, -1);
    if (s == NULL) {
      return luaL_error(
          L, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
    }
    if (i > 1) {
      buf[ind++] = '\t';
    }
    int l = strlen(s);
    if (ind + l >= buflen) {
      buflen *= 2;
      buf = xrealloc(buf, buflen);
    }
    strcpy(&buf[ind], s);
    ind += l;
    lua_pop(L, 1); /* pop result */
  }
  buf[ind++] = 0;
  ui_echom(ui, "%s", buf);
  free(buf);
  return 0;
}

static int l_error(lua_State *L) {
  ui_error(ui, "%s", luaL_optstring(L, 1, ""));
  return 0;
}

static int l_message_clear(lua_State *L) {
  (void)L;
  ui->show_message = false;
  ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_spawn(lua_State *L) {
  char **stdin = NULL;
  bool out = true;
  bool err = true;
  int out_cb_ref = -1;
  int err_cb_ref = -1;
  int cb_ref = -1;

  if (lua_gettop(L) > 2) {
    return luaL_error(L, "too many arguments");
  }

  luaL_checktype(L, 1, LUA_TTABLE); // [cmd, opts?]

  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
  }

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  char **args = NULL;
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i); // [cmd, opts?, arg]
    cvector_push_back(args, strdup(lua_tostring(L, -1)));
    lua_pop(L, 1); // [cmd, opts?]
  }
  cvector_push_back(args, NULL);
  if (lua_gettop(L) == 2) {
    lua_getfield(L, 2, "stdin"); // [cmd, opts, opts.stdin]
    if (lua_isstring(L, -1)) {
      cvector_push_back(stdin, strdup(lua_tostring(L, -1)));
    } else if (lua_istable(L, -1)) {
      const size_t m = lua_objlen(L, -1);
      for (uint32_t i = 1; i <= m; i++) {
        lua_rawgeti(L, -1, i); // [cmd, opts, opts.stdin, str]
        cvector_push_back(stdin, strdup(lua_tostring(L, -1)));
        lua_pop(L, 1); // [cmd, otps, opts.stdin]
      }
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "out"); // [cmd, opts, opts.out]
    if (lua_isfunction(L, -1)) {
      out_cb_ref = lua_set_callback(L); // [cmd, opts]
    } else {
      out = lua_toboolean(L, -1);
      lua_pop(L, 1); // [cmd, opts]
    }

    lua_getfield(L, 2, "err"); // [cmd, opts, opts.err]
    if (lua_isfunction(L, -1)) {
      err_cb_ref = lua_set_callback(L); // [cmd, opts]
    } else {
      err = lua_toboolean(L, -1);
      lua_pop(L, 1); // [cmd, opts]
    }

    lua_getfield(L, 2, "callback"); // [cmd, opts, opts.callback]
    if (lua_isfunction(L, -1)) {
      cb_ref = lua_set_callback(L); // [cmd, opts]
    } else {
      lua_pop(L, 1); // [cmd, opts]
    }
  }

  int pid = lfm_spawn(lfm, args[0], args, stdin, out, err, out_cb_ref,
                      err_cb_ref, cb_ref);

  cvector_ffree(stdin, xfree);
  cvector_ffree(args, xfree);

  if (pid != -1) {
    lua_pushnumber(L, pid);
    return 1;
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno)); // not sure if something even sets errno
    return 2;
  }
}

static int l_execute(lua_State *L) {
  if (lua_gettop(L) > 1) {
    return luaL_error(L, "too many arguments");
  }

  luaL_checktype(L, 1, LUA_TTABLE);

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  char **args = NULL;
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    cvector_push_back(args, strdup(lua_tostring(L, -1)));
    lua_pop(L, 1);
  }
  cvector_push_back(args, NULL);

  bool ret = lfm_execute(lfm, args[0], args);

  cvector_ffree(args, xfree);

  if (ret) {
    lua_pushboolean(L, true);
    return 1;
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno)); // not sure if something even sets errno
    return 2;
  }
}

static inline int map_key(lua_State *L, Trie *trie, bool allow_mode) {
  const char *keys = luaL_checkstring(L, 1);

  if (!(lua_type(L, 2) == LUA_TFUNCTION || lua_isnil(L, 2))) {
    return luaL_argerror(L, 2, "expected function or nil");
  }

  const char *desc = NULL;
  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "desc");
    if (!lua_isnoneornil(L, -1)) {
      desc = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 3, "mode");
    if (!lua_isnoneornil(L, -1)) {
      if (!allow_mode) {
        return luaL_error(L, "mode not allowed here");
      }
      struct mode *mode = ht_get(&lfm->modes, lua_tostring(L, -1));
      if (!mode) {
        return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
      }
      trie = mode->maps;
    }
    lua_pop(L, 1);
  }

  int ref = 0;
  if (!lua_isnil(L, 2)) {
    lua_pushvalue(L, 2);
    ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  int old_ref = input_map(trie, keys, ref, desc);
  if (old_ref) {
    luaL_unref(L, LUA_REGISTRYINDEX, old_ref);
  }

  return 0;
}

static int l_map_key(lua_State *L) {
  return map_key(L, lfm->maps.normal, true);
}

static int l_cmap_key(lua_State *L) {
  return map_key(L, lfm->maps.input, false);
}

static inline void lua_push_maps(lua_State *L, Trie *trie, bool prune) {
  cvector_vector_type(Trie *) keymaps = NULL;
  trie_collect_leaves(trie, &keymaps, prune);
  lua_newtable(L);
  for (size_t i = 0; i < cvector_size(keymaps); i++) {
    lua_newtable(L);
    lua_pushstring(L, keymaps[i]->desc ? keymaps[i]->desc : "");
    lua_setfield(L, -2, "desc");
    lua_pushstring(L, keymaps[i]->keys);
    lua_setfield(L, -2, "keys");
    lua_rawgeti(L, LUA_REGISTRYINDEX, keymaps[i]->ref);
    lua_setfield(L, -2, "f");
    lua_rawseti(L, -2, i + 1);
  }
}

static int l_get_maps(lua_State *L) {
  struct mode *normal = ht_get(&lfm->modes, "normal");
  lua_push_maps(L, normal->maps, luaL_optbool(L, 1, true));
  return 1;
}

static int l_get_cmaps(lua_State *L) {
  struct mode *input = ht_get(&lfm->modes, "input");
  lua_push_maps(L, input->maps, luaL_optbool(L, 1, true));
  return 1;
}

static int l_current_mode(lua_State *L) {
  lua_pushstring(L, lfm->current_mode->name);
  return 1;
}

static int l_mode(lua_State *L) {
  if (lfm_mode_enter(lfm, luaL_checkstring(L, -1)) != 0) {
    return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
  }
  struct mode *mode = lfm->current_mode;
  if (mode->input && mode->prefix) {
    cmdline_prefix_set(&ui->cmdline, mode->prefix);
  }
  return 0;
}

static int l_register_mode(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  struct mode mode = {0};

  lua_getfield(L, 1, "name");
  if (lua_isnoneornil(L, -1)) {
    return luaL_error(L, "register_mode: missing field 'name'");
  }
  mode.name = (char *)lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "input");
  mode.input = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "prefix");
  mode.prefix = lua_isnoneornil(L, -1) ? NULL : (char *)lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_enter");
  if (!lua_isnoneornil(L, -1)) {
    mode.on_enter_ref = lua_set_callback(L);
  } else {
    lua_pop(L, 1);
  }

  lua_getfield(L, 1, "on_change");
  if (!lua_isnoneornil(L, -1)) {
    mode.on_change_ref = lua_set_callback(L);
  } else {
    lua_pop(L, 1);
  }

  lua_getfield(L, 1, "on_return");
  if (!lua_isnoneornil(L, -1)) {
    mode.on_return_ref = lua_set_callback(L);
  } else {
    lua_pop(L, 1);
  }

  lua_getfield(L, 1, "on_esc");
  if (!lua_isnoneornil(L, -1)) {
    mode.on_esc_ref = lua_set_callback(L);
  } else {
    lua_pop(L, 1);
  }

  lua_getfield(L, 1, "on_exit");
  if (!lua_isnoneornil(L, -1)) {
    mode.on_exit_ref = lua_set_callback(L);
  } else {
    lua_pop(L, 1);
  }

  if (lfm_mode_register(lfm, &mode) != 0) {
    return luaL_error(L, "mode \"%s\" already exists", mode.name);
  }

  return 0;
}

int l_register_hook(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (lua_gettop(L) != 2) {
    return luaL_error(L, "function takes two only arguments");
  }

  int ref = lua_set_callback(L);
  if (streq(name, LFM_HOOK_NAME_RESIZED)) {
    lfm_add_hook(lfm, LFM_HOOK_RESIZED, ref);
  } else if (streq(name, LFM_HOOK_NAME_ENTER)) {
    lfm_add_hook(lfm, LFM_HOOK_ENTER, ref);
  } else if (streq(name, LFM_HOOK_NAME_EXITPRE)) {
    lfm_add_hook(lfm, LFM_HOOK_EXITPRE, ref);
  } else if (streq(name, LFM_HOOK_NAME_CHDIRPRE)) {
    lfm_add_hook(lfm, LFM_HOOK_CHDIRPRE, ref);
  } else if (streq(name, LFM_HOOK_NAME_CHDIRPOST)) {
    lfm_add_hook(lfm, LFM_HOOK_CHDIRPOST, ref);
  } else if (streq(name, LFM_HOOK_NAME_PASTEBUF)) {
    lfm_add_hook(lfm, LFM_HOOK_PASTEBUF, ref);
  } else if (streq(name, LFM_HOOK_NAME_DIRLOADED)) {
    lfm_add_hook(lfm, LFM_HOOK_DIRLOADED, ref);
  } else if (streq(name, LFM_HOOK_NAME_DIRUPDATED)) {
    lfm_add_hook(lfm, LFM_HOOK_DIRUPDATED, ref);
  } else if (streq(name, LFM_HOOK_NAME_MODECHANGED)) {
    lfm_add_hook(lfm, LFM_HOOK_MODECHANGED, ref);
  } else {
    return luaL_error(L, "no such hook: %s", name);
  }

  return 0;
}

static const struct luaL_Reg lfm_lib[] = {{"mode", l_mode},
                                          {"current_mode", l_current_mode},
                                          {"register_mode", l_register_mode},
                                          {"register_hook", l_register_hook},
                                          {"schedule", l_schedule},
                                          {"colors_clear", l_colors_clear},
                                          {"execute", l_execute},
                                          {"spawn", l_spawn},
                                          {"map", l_map_key},
                                          {"cmap", l_cmap_key},
                                          {"get_maps", l_get_maps},
                                          {"get_cmaps", l_get_cmaps},
                                          {"handle_key", l_handle_key},
                                          {"find", l_find},
                                          {"find_clear", l_find_clear},
                                          {"find_next", l_find_next},
                                          {"find_prev", l_find_prev},
                                          {"nohighlight", l_nohighlight},
                                          {"search", l_search},
                                          {"search_back", l_search_backwards},
                                          {"search_next", l_search_next},
                                          {"search_prev", l_search_prev},
                                          {"crash", l_crash},
                                          {"print", l_print},
                                          {"error", l_error},
                                          {"message_clear", l_message_clear},
                                          {"quit", l_quit},
                                          {NULL, NULL}};

static int l_modes_index(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  struct mode *mode = ht_get(&lfm->modes, key);
  if (!mode) {
    return 0;
  }

  lua_newtable(L);
  struct mode **ud = lua_newuserdata(L, sizeof mode);
  *ud = mode;
  luaL_newmetatable(L, LFM_MODE_META);
  lua_setmetatable(L, -2);

  return 1;
}

static int l_mode_index(lua_State *L) {
  struct mode *mode = *(struct mode **)luaL_checkudata(L, 1, LFM_MODE_META);
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "name")) {
    lua_pushstring(L, mode->name);
    return 1;
  } else if (streq(key, "prefix")) {
    lua_pushstring(L, mode->prefix);
    return 1;
  } else if (streq(key, "input")) {
    lua_pushboolean(L, mode->input);
    return 1;
  } else {
    return luaL_error(L, "no such field: %s", key);
  }
  return 0;
}

static int l_mode_newindex(lua_State *L) {
  struct mode *mode = *(struct mode **)luaL_checkudata(L, 1, LFM_MODE_META);
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "prefix")) {
    if (!mode->input) {
      return luaL_error(L, "can only set prefix for input modes");
    }
    const char *prefix = lua_isnoneornil(L, 3) ? "" : lua_tostring(L, 3);
    free(mode->prefix);
    mode->prefix = strdup(prefix);
  } else {
    return luaL_error(L, "no such field: %s", key);
  }
  return 0;
}

static const struct luaL_Reg lfm_modes_mt[] = {{"__index", l_modes_index},
                                               {NULL, NULL}};

static const struct luaL_Reg lfm_mode_mt[] = {
    {"__index", l_mode_index}, {"__newindex", l_mode_newindex}, {NULL, NULL}};

int luaopen_lfm(lua_State *L) {
  log_debug("%d", lua_gettop(L));
  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");

  luaL_openlib(L, "lfm", lfm_lib, 0); // [lfm]

  luaopen_fm(L);
  lua_setfield(L, -2, "fm");

  luaopen_config(L);
  lua_setfield(L, -2, "config");

  luaopen_log(L);
  lua_setfield(L, -2, "log");

  luaopen_ui(L);
  lua_setfield(L, -2, "ui");

  luaopen_cmd(L);
  lua_setfield(L, -2, "cmd");

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");

  luaopen_rifle(L);
  lua_setfield(L, -2, "rifle");

  luaL_newmetatable(L, LFM_MODE_META);
  luaL_register(L, NULL, lfm_mode_mt);
  lua_pop(L, 1);

  lua_newtable(L);
  luaL_newmetatable(L, LFM_MODES_META);
  luaL_register(L, NULL, lfm_modes_mt);
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "modes");

  lua_newtable(L);
  lua_pushstring(L, LFM_VERSION);
  lua_setfield(L, -2, "info");

  lua_pushstring(L, LFM_REVCOUNT);
  lua_setfield(L, -2, "revcount");

  lua_pushstring(L, LFM_COMMIT);
  lua_setfield(L, -2, "commit");

  lua_pushstring(L, LFM_BUILD_TYPE);
  lua_setfield(L, -2, "build_type");

  lua_pushstring(L, LFM_BRANCH);
  lua_setfield(L, -2, "branch");
  lua_setfield(L, -2, "version");

  lua_pop(L, 1); // []

  return 1;
}
