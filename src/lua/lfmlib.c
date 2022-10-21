#include <errno.h>
#include <lauxlib.h>

#include "auto/versiondef.h"
#include "cmd.h"
#include "config.h"
#include "fm.h"
#include "fn.h"
#include "internal.h"
#include "lfmlua.h"
#include "log.h"
#include "rifle.h"
#include "ui.h"

#include "../config.h"
#include "../find.h"
#include "../input.h"
#include "../log.h"
#include "../search.h"

static int l_schedule(lua_State *L)
{
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

static int l_colors_clear(lua_State *L)
{
  (void) L;
  config_colors_clear();
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_handle_key(lua_State *L)
{
  const char *keys = luaL_checkstring(L, 1);
  input_t *buf = malloc((strlen(keys) + 1) * sizeof *buf);
  key_names_to_input(keys, buf);
  for (input_t *u = buf; *u; u++) {
    lfm_handle_key(lfm, *u);
  }
  free(buf);
  return 0;
}

static int l_timeout(lua_State *L)
{
  const int32_t dur = luaL_checkinteger(L, 1);
  if (dur > 0) {
    input_timeout_set(lfm, dur);
  }
  return 0;
}

static int l_search(lua_State *L)
{
  search(lfm, luaL_optstring(L, 1, NULL), true);
  return 0;
}

static int l_search_backwards(lua_State *L)
{
  search(lfm, luaL_optstring(L, 1, NULL), false);
  return 0;
}

static int l_nohighlight(lua_State *L)
{
  (void) L;
  search_nohighlight(lfm);
  return 0;
}

static int l_search_next(lua_State *L)
{
  search_next(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_search_prev(lua_State *L)
{
  search_prev(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_find(lua_State *L)
{
  lua_pushboolean(L, find(fm, luaL_checkstring(L, 1)));
  return 1;
}

static int l_find_clear(lua_State *L)
{
  (void) L;
  find_clear(fm);
  return 0;
}

static int l_find_next(lua_State *L)
{
  (void) L;
  find_next(fm);
  return 0;
}

static int l_find_prev(lua_State *L)
{
  (void) L;
  find_prev(fm);
  return 0;
}

static int l_crash(lua_State *L)
{
  free(L);
  return 0;
}

static int l_quit(lua_State *L)
{
  lfm_quit(lfm);
  // hand back control to the C caller
  luaL_error(L, "quit");
  return 0;
}

static int l_echo(lua_State *L)
{
  ui_echom(ui, "%s", luaL_optstring(L, 1, ""));
  return 0;
}

static int l_error(lua_State *L)
{
  ui_error(ui, luaL_checkstring(L, 1));
  return 0;
}

static int l_message_clear(lua_State *L)
{
  (void) L;
  ui->message = false;
  ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

static int l_spawn(lua_State *L)
{
  char **stdin = NULL;
  bool out = true;
  bool err = true;
  int out_cb_ref = -1;
  int err_cb_ref = -1;
  int cb_ref = -1;

  luaL_checktype(L, 1, LUA_TTABLE);

  const int n = lua_objlen(L, 1);
  if (n == 0) {
    luaL_error(L, "no command given");
  }

  char **args = NULL;
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    cvector_push_back(args, strdup(lua_tostring(L, -1)));
    lua_pop(L, 1);
  }
  cvector_push_back(args, NULL);
  if (lua_gettop(L) >= 2) {
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_getfield(L, 2, "stdin");
    if (lua_isstring(L, -1)) {
      cvector_push_back(stdin, strdup(lua_tostring(L, -1)));
    } else if (lua_istable(L, -1)) {
      const size_t m = lua_objlen(L, -1);
      for (uint32_t i = 1; i <= m; i++) {
        lua_rawgeti(L, -1, i);
        cvector_push_back(stdin, strdup(lua_tostring(L, -1)));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, 2, "out");
    if (lua_isfunction(L, -1)) {
      out_cb_ref = lua_set_callback(L);
    } else {
      out = lua_toboolean(L, -1);
      lua_pop(L, 1);
    }

    lua_getfield(L, 2, "err");
    if (lua_isfunction(L, -1)) {
      err_cb_ref = lua_set_callback(L);
    } else {
      err = lua_toboolean(L, -1);
      lua_pop(L, 1);
    }

    lua_getfield(L, 2, "callback");
    if (lua_isfunction(L, -1)) {
      cb_ref = lua_set_callback(L);
    } else {
      lua_pop(L, 1);
    }
  }

  int pid = lfm_spawn(lfm, args[0], args, stdin, out, err, out_cb_ref, err_cb_ref, cb_ref);

  cvector_ffree(stdin, free);
  cvector_ffree(args, free);

  if (pid != -1) {
    lua_pushnumber(L, pid);
    return 1;
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno)); // not sure if something even sets errno
    return 2;
  }
}

static int l_execute(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TTABLE);

  const int n = lua_objlen(L, 1);
  if (n == 0) {
    luaL_error(L, "no command given");
  }

  char **args = NULL;
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    cvector_push_back(args, strdup(lua_tostring(L, -1)));
    lua_pop(L, 1);
  }
  cvector_push_back(args, NULL);

  bool ret = lfm_execute(lfm, args[0], args);

  cvector_ffree(args, free);

  if (ret) {
    lua_pushboolean(L, true);
    return 1;
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno)); // not sure if something even sets errno
    return 2;
  }
}

static inline int map_key(lua_State *L, Trie *trie)
{
  if (!(lua_type(L, 2) == LUA_TFUNCTION || lua_isnil(L, 2))) {
    luaL_argerror(L, 2, "expected function or nil");
  }

  const char *desc = NULL;
  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "desc");
    if (!lua_isnoneornil(L, -1)) {
      desc = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
  }
  const char *keys = luaL_checkstring(L, 1);

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

static int l_map_key(lua_State *L)
{
  return map_key(L, lfm->maps.normal);
}

static int l_cmap_key(lua_State *L)
{
  return map_key(L, lfm->maps.cmd);
}

static inline void lua_push_maps(lua_State *L, Trie *trie, bool prune)
{
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

static int l_get_maps(lua_State *L)
{
  lua_push_maps(L, lfm->maps.normal, luaL_optbool(L, 1, true));
  return 1;
}

static int l_get_cmaps(lua_State *L)
{
  lua_push_maps(L, lfm->maps.cmd, luaL_optbool(L, 1, true));
  return 1;
}

static const struct luaL_Reg lfm_lib[] = {
  {"schedule", l_schedule},
  {"colors_clear", l_colors_clear},
  {"execute", l_execute},
  {"spawn", l_spawn},
  {"map", l_map_key},
  {"cmap", l_cmap_key},
  {"get_maps", l_get_maps},
  {"get_cmaps", l_get_cmaps},
  {"handle_key", l_handle_key},
  {"timeout", l_timeout},
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
  {"echo", l_echo},
  {"error", l_error},
  {"message_clear", l_message_clear},
  {"quit", l_quit},
  {NULL, NULL}};

int luaopen_lfm(lua_State *L)
{
  log_debug("opening lualfm libs");

  luaL_openlib(L, "lfm", lfm_lib, 0);

  luaopen_fm(L);
  lua_setfield(L, -2, "fm");

  luaopen_config(L);
  lua_setfield(L, -2, "config");  // lfm.config

  luaopen_log(L);
  lua_setfield(L, -2, "log");  // lfm.log

  luaopen_ui(L);
  lua_setfield(L, -2, "ui");  // lfm.ui

  luaopen_cmd(L);
  lua_setfield(L, -2, "cmd");  // lfm.cmd

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");  // lfm.fn

  luaopen_rifle(L);
  lua_setfield(L, -2, "rifle"); // lfm.rifle

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
  lua_setfield(L, -2, "version");  // lfm.version

  return 1;
}
