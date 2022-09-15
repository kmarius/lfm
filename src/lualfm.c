#include <errno.h>
#include <lauxlib.h>
#include <libgen.h>
#include <limits.h>
#include <luajit.h>
#include <lualib.h>
#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "async.h"
#include "cmdline.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "find.h"
#include "fm.h"
#include "hashtab.h"
#include "loader.h"
#include "log.h"
#include "lua.h"
#include "lualfm.h"
#include "ncutil.h"
#include "notify.h"
#include "rifle.h"
#include "search.h"
#include "tokenize.h"
#include "tpool.h"
#include "trie.h"
#include "ui.h"
#include "util.h"

#define luaL_optbool(L, i, d) \
  lua_isnoneornil(L, i) ? d : lua_toboolean(L, i)

static Lfm *lfm = NULL;
static Ui *ui = NULL;
static Fm *fm = NULL;

static struct {
  Trie *normal;  // normal mode mappings
  Trie *cmd;     // command mode mappings
  Trie *cur;     // pointer to the current leaf in either of the tries
  input_t *seq;  // current key sequence
  int count;
  bool accept_count;
} maps;

/* lfm lib {{{ */

// stores the function on top of the stack in the registry and returns the
// reference index
static inline int lua_set_callback(lua_State *L)
{
  return luaL_ref(L, LUA_REGISTRYINDEX);
}


static inline bool lua_get_callback(lua_State *L, int ref, bool unref)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (unref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  return lua_type(L, -1) == LUA_TFUNCTION;
}


void lua_run_callback(lua_State *L, int ref)
{
  if (lua_get_callback(L, ref, true)) {
    if (lua_pcall(L, 0, 0, 0)) {
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}


void lua_run_child_callback(lua_State *L, int ref, int rstatus)
{
  if (lua_get_callback(L, ref, true)) {
    lua_pushnumber(L, rstatus);
    if (lua_pcall(L, 1, 0, 0)) {
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}


void lua_run_stdout_callback(lua_State *L, int ref, const char *line)
{
  if (lua_get_callback(L, ref, line == NULL) && line) {
    lua_pushstring(L, line);
    lua_insert(L, -1);
    if (lua_pcall(L, 1, 0, 0)) {
      ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
    }
  }
}


static int l_schedule(lua_State *L)
{
  luaL_checktype(L, 1, LUA_TFUNCTION);
  const int delay = luaL_checknumber(L, 2);
  lua_pushvalue(L, 1);
  if (delay > 0) {
    lfm_schedule(lfm, lua_set_callback(L), delay);
  } else {
    if (lua_pcall(L, 0, 0, 0)) {
      ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
    }
  }
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
    lua_handle_key(L, *u);
  }
  free(buf);
  return 0;
}


static int l_timeout(lua_State *L)
{
  const int32_t dur = luaL_checkinteger(L, 1);
  if (dur > 0) {
    lfm_timeout_set(lfm, dur);
  }
  return 0;
}


static int l_search(lua_State *L)
{
  search(ui, luaL_optstring(L, 1, NULL), true);
  return 0;
}


static int l_search_backwards(lua_State *L)
{
  search(ui, luaL_optstring(L, 1, NULL), false);
  return 0;
}


static int l_nohighlight(lua_State *L)
{
  (void) L;
  nohighlight(ui);
  return 0;
}


static int l_search_next(lua_State *L)
{
  (void) L;
  search_next(ui, fm, luaL_optbool(L, 1, false));
  return 0;
}


static int l_search_prev(lua_State *L)
{
  (void) L;
  search_prev(ui, fm, luaL_optbool(L, 1, false));
  return 0;
}


static int l_find(lua_State *L)
{
  lua_pushboolean(L, find(fm, ui, luaL_checkstring(L, 1)));
  return 1;
}


static int l_find_clear(lua_State *L)
{
  (void) L;
  find_clear(fm, ui);
  return 0;
}


static int l_find_next(lua_State *L)
{
  (void) L;
  find_next(fm, ui);
  return 0;
}


static int l_find_prev(lua_State *L)
{
  (void) L;
  find_prev(fm, ui);
  return 0;
}


static int l_crash(lua_State *L)
{
  free(L);
  return 0;
}


static int l_quit(lua_State *L)
{
  (void) L;
  lfm_quit(lfm);
  return 0;
}


static int l_echo(lua_State *L)
{
  ui_echom(ui, luaL_optstring(L, 1, ""));
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

  const uint32_t n = lua_objlen(L, 1);
  if (n == 0) {
    luaL_error(L, "no command given");
  }

  char **args = malloc((n + 1) * sizeof *args);
  for (uint32_t i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    args[i-1] = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  args[n] = NULL;
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
  for (uint32_t i = 0; i < n; i++) {
    free(args[i]);
  }
  free(args);

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

  const uint32_t n = lua_objlen(L, 1);
  if (n == 0) {
    luaL_error(L, "no command given");
  }

  char **args = malloc((n + 1) * sizeof *args);
  for (uint32_t i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, i);
    args[i-1] = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  args[n] = NULL;
  bool ret = lfm_execute(lfm, args[0], args);
  for (uint32_t i = 0; i < n; i++) {
    free(args[i]);
  }
  free(args);

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
  const char *desc = NULL;
  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "desc");
    if (!lua_isnoneornil(L, -1)) {
      desc = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
  }

  const char *keys = luaL_checkstring(L, 1);

  if (!(lua_type(L, 2) == LUA_TFUNCTION || lua_isnil(L, 2))) {
    luaL_argerror(L, 2, "expected function or nil");
  }

  input_t *buf = malloc((strlen(keys) + 1) * sizeof *buf);
  Trie *ptr;
  if (!lua_isnil(L, 2)) {
    ptr = trie_insert(trie, key_names_to_input(keys, buf), keys, desc);
  } else {
    ptr = trie_remove(trie, key_names_to_input(keys, buf));
  }
  free(buf);

  if (ptr) {
    lua_pushlightuserdata(L, (void *) ptr);
    lua_pushvalue(L, 2);
    lua_settable(L, LUA_REGISTRYINDEX);
  }
  return 0;
}


static int l_map_key(lua_State *L)
{
  return map_key(L, maps.normal);
}


static int l_cmap_key(lua_State *L)
{
  return map_key(L, maps.cmd);
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
    lua_pushlightuserdata(L, (void *) keymaps[i]);
    lua_gettable(L, LUA_REGISTRYINDEX);
    lua_setfield(L, -2, "f");
    lua_rawseti(L, -2, i + 1);
  }
}


static int l_get_maps(lua_State *L)
{
  lua_push_maps(L, maps.normal, luaL_optbool(L, 1, true));
  return 1;
}


static int l_get_cmaps(lua_State *L)
{
  lua_push_maps(L, maps.cmd, luaL_optbool(L, 1, true));
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

/* }}} */

/* config lib {{{ */

static int l_config_index(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "truncatechar")) {
    char buf[MB_LEN_MAX + 1];
    int l = wctomb(buf, cfg.truncatechar);
    if (l == -1) {
      log_error("converting truncatechar to mbs");
      l = 0;
    }
    buf[l] = 0;
    lua_pushstring(L, buf);
    return 1;
  } else if (streq(key, "hidden")) {
    lua_pushboolean(L, cfg.hidden);
    return 1;
  } else if (streq(key, "ratios")) {
    const size_t l = cvector_size(cfg.ratios);
    lua_createtable(L, l, 0);
    for (size_t i = 0; i < l; i++) {
      lua_pushinteger(L, cfg.ratios[i]);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  } else if (streq(key, "inotify_blacklist")) {
    const size_t l = cvector_size(cfg.inotify_blacklist);
    lua_createtable(L, l, 0);
    for (size_t i = 0; i < l; i++) {
      lua_pushstring(L, cfg.inotify_blacklist[i]);
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  } else if (streq(key, "inotify_timeout")) {
    lua_pushinteger(L, cfg.inotify_timeout);
    return 1;
  } else if (streq(key, "inotify_delay")) {
    lua_pushinteger(L, cfg.inotify_delay);
    return 1;
  } else if (streq(key, "scrolloff")) {
    lua_pushinteger(L, cfg.scrolloff);
    return 1;
  } else if (streq(key, "preview")) {
    lua_pushboolean(L, cfg.preview);
    return 1;
  } else if (streq(key, "preview_images")) {
    lua_pushboolean(L, cfg.preview_images);
    return 1;
  } else if (streq(key, "image_extensions")) {
    lua_createtable(L, cfg.image_extensions->size, 0);
    size_t i = 1;
    ht_foreach(const char *ext, cfg.image_extensions) {
      lua_pushstring(L, ext);
      lua_rawseti(L, -2, i++);
    }
    return 1;
  } else if (streq(key, "previewer")) {
    lua_pushstring(L, cfg.previewer ? cfg.previewer : "");
    return 1;
  } else if (streq(key, "fifopath")) {
    lua_pushstring(L, cfg.fifopath);
    return 1;
  } else if (streq(key, "logpath")) {
    lua_pushstring(L, cfg.logpath);
    return 1;
  } else if (streq(key, "configpath")) {
    lua_pushstring(L, cfg.configpath);
    return 1;
  } else if (streq(key, "configdir")) {
    lua_pushstring(L, cfg.configdir);
    return 1;
  } else if (streq(key, "luadir")) {
    lua_pushstring(L, cfg.luadir);
    return 1;
  } else if (streq(key, "datadir")) {
    lua_pushstring(L, cfg.datadir);
    return 1;
  } else if (streq(key, "user_datadir")) {
    lua_pushstring(L, cfg.user_datadir);
    return 1;
  } else if (streq(key, "runtime_dir")) {
    lua_pushstring(L, cfg.rundir);
    return 1;
  } else {
    luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}


static int l_config_newindex(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "truncatechar")) {
    wchar_t w;
    const char *val = luaL_checkstring(L, 3);
    int l = mbtowc(&w, val, MB_LEN_MAX);
    if (l == -1) {
      log_error("converting truncatechar to wchar_t");
      return 0;
    }
    cfg.truncatechar = w;
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "hidden")) {
    bool hidden = lua_toboolean(L, 3);
    fm_hidden_set(fm, hidden);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "ratios")) {
    const size_t l = lua_objlen(L, 3);
    if (l == 0) {
      luaL_argerror(L, 3, "no ratios given");
    }
    uint32_t *ratios = NULL;
    for (uint32_t i = 1; i <= l; i++) {
      lua_rawgeti(L, 3, i);
      cvector_push_back(ratios, lua_tointeger(L, -1));
      if (ratios[i-1] <= 0) {
        luaL_error(L, "ratio must be non-negative");
        cvector_free(ratios);
        return 0;
      }
      lua_pop(L, 1);
    }
    config_ratios_set(ratios);
    fm_recol(fm);
    ui_recol(ui);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "inotify_blacklist")) {
    const size_t l = lua_objlen(L, 3);
    cvector_ffree(cfg.inotify_blacklist, free);
    cfg.inotify_blacklist = NULL;
    for (size_t i = 1; i <= l; i++) {
      lua_rawgeti(L, 3, i);
      cvector_push_back(cfg.inotify_blacklist, strdup(lua_tostring(L, -1)));
      lua_pop(L, 1);
    }
    return 0;
  } else if (streq(key, "inotify_timeout")) {
    int n = luaL_checkinteger(L, 3);
    if (n < 100) {
      luaL_argerror(L, 3, "timeout must be larger than 100");
    }
    cfg.inotify_timeout = n;
    loader_reschedule(&lfm->loader);
    return 0;
  } else if (streq(key, "inotify_delay")) {
    int n = luaL_checkinteger(L, 3);
    cfg.inotify_delay = n;
    loader_reschedule(&lfm->loader);
    return 0;
  } else if (streq(key, "scrolloff")) {
    cfg.scrolloff = max(luaL_checkinteger(L, 3), 0);
    return 0;
  } else if (streq(key, "preview")) {
    cfg.preview = lua_toboolean(L, 3);
    if (cfg.preview) {
      ui_drop_cache(ui);
    }
    fm_recol(fm);
    ui_redraw(ui, REDRAW_FM);
    return 0;
  } else if (streq(key, "preview_images")) {
    cfg.preview_images = lua_toboolean(L, 3);
    fm_recol(fm);
    /* TODO: check if the setting changed when loading previews
     * instead of dropping the cache (on 2022-09-14) */
    ui_drop_cache(ui);
    ui_redraw(ui, REDRAW_PREVIEW);
    return 0;
  } else if (streq(key, "image_extensions")) {
    const size_t l = lua_objlen(L, 3);
    ht_clear(cfg.image_extensions);
    for (uint32_t i = 1; i <= l; i++) {
      lua_rawgeti(L, 3, i);
      image_extension_add(lua_tostring(L, -1));
      lua_pop(L, 1);
    }
    ui_drop_cache(ui);
    ui_redraw(ui, REDRAW_PREVIEW);
  } else if (streq(key, "previewer")) {
    if (lua_isnoneornil(L, 3)) {
      free(cfg.previewer);
      cfg.previewer = NULL;
    } else {
      const char *str = luaL_checkstring(L, 3);
      free(cfg.previewer);
      cfg.previewer = str[0] != 0 ? path_replace_tilde(str) : NULL;
    }
    ui_drop_cache(ui);
    return 0;
  } else {
    luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}


static const struct luaL_Reg config_mt[] = {
  {"__index", l_config_index},
  {"__newindex", l_config_newindex},
  {NULL, NULL}};

/* }}} */

/* log lib {{{ */

static int l_log_trace(lua_State *L)
{
  log_trace("%s", luaL_checkstring(L, 1));
  return 0;
}


static int l_log_debug(lua_State *L)
{
  log_debug("%s", luaL_checkstring(L, 1));
  return 0;
}


static int l_log_info(lua_State *L)
{
  log_info("%s", luaL_checkstring(L, 1));
  return 0;
}


static int l_log_warn(lua_State *L)
{
  log_warn("%s", luaL_checkstring(L, 1));
  return 0;
}


static int l_log_error(lua_State *L)
{
  log_error("%s", luaL_checkstring(L, 1));
  return 0;
}


static int l_log_fatal(lua_State *L)
{
  log_fatal("%s", luaL_checkstring(L, 1));
  return 0;
}


static const struct luaL_Reg log_lib[] = {
  {"trace", l_log_trace},
  {"debug", l_log_debug},
  {"info", l_log_info},
  {"warn", l_log_warn},
  {"error", l_log_error},
  {"fatal", l_log_fatal},
  {NULL, NULL}};

/* }}} */

/* ui lib {{{ */

static int l_ui_history_lfmend(lua_State *L)
{
  history_lfmend(&ui->history, luaL_checkstring(L, 1));
  return 0;
}


static int l_ui_history_prev(lua_State *L)
{
  const char *line = history_prev(&ui->history);
  if (!line) {
    return 0;
  }
  lua_pushstring(L, line);
  return 1;
}


static int l_ui_history_next(lua_State *L)
{
  const char *line = history_next(&ui->history);
  if (!line) {
    return 0;
  }
  lua_pushstring(L, line);
  return 1;
}


static int l_ui_messages(lua_State *L)
{
  lua_newtable(L);
  for (size_t i = 0; i < cvector_size(ui->messages); i++) {
    lua_pushstring(L, ui->messages[i]);
    lua_rawseti(L, -2, i+1);
  }
  return 1;
}


static int l_ui_clear(lua_State *L)
{
  (void) L;
  ui_clear(ui);
  return 0;
}


static int l_ui_get_width(lua_State *L)
{
  lua_pushnumber(L, ui->ncol);
  return 1;
}


static int l_ui_get_height(lua_State *L)
{
  lua_pushnumber(L, ui->nrow);
  return 1;
}


static int l_ui_menu(lua_State *L)
{
  cvector_vector_type(char*) menubuf = NULL;
  if (lua_type(L, -1) == LUA_TTABLE) {
    cvector_grow(menubuf, lua_objlen(L, -1));
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
      cvector_push_back(menubuf, strdup(luaL_checkstring(L, -1)));
    }
  }
  ui_menu_show(ui, menubuf);
  return 0;
}


static int l_ui_draw(lua_State *L)
{
  (void) L;
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_notcurses_canopen_images(lua_State *L)
{
  lua_pushboolean(L, notcurses_canopen_images(ui->nc));
  return 1;
}

static int l_notcurses_canbraille(lua_State *L)
{
  lua_pushboolean(L, notcurses_canbraille(ui->nc));
  return 1;
}

static int l_notcurses_canpixel(lua_State *L)
{
  lua_pushboolean(L, notcurses_canpixel(ui->nc));
  return 1;
}

static int l_notcurses_canquadrant(lua_State *L)
{
  lua_pushboolean(L, notcurses_canquadrant(ui->nc));
  return 1;
}

static int l_notcurses_cansextant(lua_State *L)
{
  lua_pushboolean(L, notcurses_cansextant(ui->nc));
  return 1;
}

static int l_notcurses_canhalfblock(lua_State *L)
{
  lua_pushboolean(L, notcurses_canhalfblock(ui->nc));
  return 1;
}


static const struct luaL_Reg ui_lib[] = {
  {"notcurses_canopen_images", l_notcurses_canopen_images},
  {"notcurses_canhalfblock", l_notcurses_canhalfblock},
  {"notcurses_canquadrant", l_notcurses_canquadrant},
  {"notcurses_cansextant", l_notcurses_cansextant},
  {"notcurses_canbraille", l_notcurses_canbraille},
  {"notcurses_canpixel", l_notcurses_canpixel},
  {"get_width", l_ui_get_width},
  {"get_height", l_ui_get_height},
  {"clear", l_ui_clear},
  {"draw", l_ui_draw},
  {"history_lfmend", l_ui_history_lfmend},
  {"history_next", l_ui_history_next},
  {"history_prev", l_ui_history_prev},
  {"menu", l_ui_menu},
  {"messages", l_ui_messages},
  {NULL, NULL}};

/* }}} */

/* color lib {{{ */

static inline uint32_t read_channel(lua_State *L, int idx)
{
  switch(lua_type(L, idx))
  {
    case LUA_TSTRING:
      return NCCHANNEL_INITIALIZER_PALINDEX(lua_tointeger(L, idx));
    case LUA_TNUMBER:
      return NCCHANNEL_INITIALIZER_HEX(lua_tointeger(L, idx));
    default:
      luaL_typerror(L, idx, "string or number");
      return 0;
  }
}


static inline uint64_t read_color_pair(lua_State *L, int ind)
{
  uint32_t fg, bg = fg = 0;
  ncchannel_set_default(&fg);
  ncchannel_set_default(&bg);

  lua_getfield(L, ind, "fg");
  if (!lua_isnoneornil(L, -1)) {
    fg = read_channel(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, ind, "bg");
  if (!lua_isnoneornil(L, -1)) {
    bg = read_channel(L, -1);
  }
  lua_pop(L, 1);

  return ncchannels_combine(fg, bg);
}


static int l_colors_newindex(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "copy")) {
    if (lua_istable(L, 3)) {
      cfg.colors.copy = read_color_pair(L, 3);
    }
  } else if (streq(key, "delete")) {
    if (lua_istable(L, 3)) {
      cfg.colors.delete = read_color_pair(L, 3);
    }
  } else if (streq(key, "dir")) {
    if (lua_istable(L, 3)) {
      cfg.colors.dir = read_color_pair(L, 3);
    }
  } else if (streq(key, "broken")) {
    if (lua_istable(L, 3)) {
      cfg.colors.broken = read_color_pair(L, 3);
    }
  } else if (streq(key, "exec")) {
    if (lua_istable(L, 3)) {
      cfg.colors.exec = read_color_pair(L, 3);
    }
  } else if (streq(key, "search")) {
    if (lua_istable(L, 3)) {
      cfg.colors.search = read_color_pair(L, 3);
    }
  } else if (streq(key, "normal")) {
    if (lua_istable(L, 3)) {
      cfg.colors.normal = read_color_pair(L, 3);
    }
  } else if (streq(key, "current")) {
    cfg.colors.current = read_channel(L, 3);
  } else if (streq(key, "patterns")) {
    if (lua_istable(L, 3)) {
      for (lua_pushnil(L); lua_next(L, 3); lua_pop(L, 1)) {
        lua_getfield(L, -1, "color");
        const uint64_t ch = read_color_pair(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "ext");
        for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
          config_ext_channel_add(lua_tostring(L, -1), ch);
        }
        lua_pop(L, 1);
      }
    }
  } else {
    luaL_error(L, "unexpected key %s", key);
  }
  ui_redraw(ui, REDRAW_FM);
  return 0;
}


static const struct luaL_Reg colors_mt[] = {
  {"__newindex", l_colors_newindex},
  {NULL, NULL}};

/* }}} */

/* cmd lib {{{ */

static int l_cmd_line_get(lua_State *L)
{
  lua_pushstring(L, cmdline_get(&ui->cmdline));
  return 1;
}


// TODO: when setting the prefix we also need to enable the cursor
// see ui_cmdline_prefix_set (on 2022-03-28)
static int l_cmd_line_set(lua_State *L)
{
  ui->message = false;
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
    default:
      luaL_error(L, "line_get takes up to three arguments");
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


static const struct luaL_Reg cmd_lib[] = {
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

/* }}} */

/* fm lib {{{ */

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
    lua_run_hook(L, LFM_HOOK_CHDIRPOST);
  }
  nohighlight(ui);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}


static int l_fm_open(lua_State *L)
{
  File *file = fm_open(fm);
  if (!file) {
    lua_run_hook(L, LFM_HOOK_CHDIRPOST);
    /* changed directory */
    ui_redraw(ui, REDRAW_FM);
    nohighlight(ui);
    return 0;
  } else {
    if (cfg.selfile) {
      /* lastdir is written in main */
      fm_selection_write(fm, cfg.selfile);
      lfm_quit(lfm);
      return 0;
    }

    lua_pushstring(L, file_path(file));
    return 1;
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
  const char *op;
  Dir *dir = fm_current_dir(fm);
  for (int i = 0; i < l; i++) {
    op = luaL_checkstring(L, i + 1);
    if (streq(op, "name")) {
      dir->sorttype = SORT_NAME;
    } else if (streq(op, "natural")) {
      dir->sorttype = SORT_NATURAL;
    } else if (streq(op, "ctime")) {
      dir->sorttype = SORT_CTIME;
    } else if (streq(op, "size")) {
      dir->sorttype = SORT_SIZE;
    } else if (streq(op, "random")) {
      dir->sorttype = SORT_RAND;
    } else if (streq(op, "dirfirst")) {
      dir->dirfirst = true;
    } else if (streq(op, "nodirfirst")) {
      dir->dirfirst = false;
    } else if (streq(op, "reverse")) {
      dir->reverse = true;
    } else if (streq(op, "noreverse")) {
      dir->reverse = false;
    } else {
      luaL_error(L, "sortby: unrecognized option: %s", op);
      // not reached
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
  fm_selection_clear(fm);
  if (lua_istable(L, -1)) {
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
  size_t i = 1;
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
  char *path = path_qualify(luaL_optstring(L, 1, "~"));
  nohighlight(ui);
  lua_run_hook(L, LFM_HOOK_CHDIRPRE);
  if (fm_chdir(fm, path, true)) {
    lua_run_hook(L, LFM_HOOK_CHDIRPOST);
  }
  ui_redraw(ui, REDRAW_FM);
  free(path);
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
    error("unrecognized paste mode: %s", mode);
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
    error("unrecognized paste mode: %s", mode);
  }

  if (luaL_optbool(L, 3, true)) {
    lua_run_hook(L, LFM_HOOK_PASTEBUF);
  }

  ui_redraw(ui, REDRAW_FM);

  return 0;
}


static int l_fm_copy(lua_State *L)
{
  fm_paste_mode_set(fm, PASTE_MODE_COPY);
  lua_run_hook(L, LFM_HOOK_PASTEBUF);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}


static int l_fm_cut(lua_State *L)
{
  fm_paste_mode_set(fm, PASTE_MODE_MOVE);
  lua_run_hook(L, LFM_HOOK_PASTEBUF);
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
  const char *filter = lua_tostring(L, 1);
  fm_filter(fm, filter);
  ui_redraw(ui, REDRAW_FM);
  return 0;
}


static int l_fm_jump_automark(lua_State *L)
{
  (void) L;
  lua_run_hook(L, LFM_HOOK_CHDIRPRE);
  if (fm_jump_automark(fm)) {
    lua_run_hook(L, LFM_HOOK_CHDIRPOST);
  }
  ui_redraw(ui, REDRAW_FM);
  return 0;
}


static int l_fm_flatten_level(lua_State *L)
{
  log_debug("flatten_level %d", fm_current_dir(fm)->flatten_level);
  lua_pushinteger(L, fm_current_dir(fm)->flatten_level);
  return 1;
}

static int l_fm_flatten(lua_State *L)
{
  (void) L;
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
  {"drop_cache", l_fm_drop_cache},
  {"reload", l_fm_reload},
  {"sel", l_fm_sel},
  {"get_height", l_fm_get_height},
  {NULL, NULL}};

/* }}} */

/* fn lib {{{ */

static int l_fn_mime(lua_State *L)
{
  char mime[MIME_MAX];
  const char *path = luaL_checkstring(L, 1);
  if (get_mimetype(path, mime)) {
    lua_pushstring(L, mime);
    return 1;
  }
  return 0;
}


static int l_fn_tokenize(lua_State *L)
{
  const char *string = luaL_optstring(L, 1, "");
  char *buf = malloc(strlen(string) + 1);
  const char *pos1, *tok;
  char *pos2;
  if ((tok = tokenize(string, buf, &pos1, &pos2))) {
    lua_pushstring(L, tok);
  }
  lua_newtable(L);
  int i = 1;
  while ((tok = tokenize(NULL, NULL, &pos1, &pos2))) {
    lua_pushstring(L, tok);
    lua_rawseti(L, -2, i++);
  }
  free(buf);
  return 2;
}


static int l_fn_split_last(lua_State *L)
{
  const char *s, *string = luaL_checkstring(L, 1);
  const char *last = string; /* beginning of last token */
  bool esc = false;
  for (s = string; *s != 0; s++) {
    if (*s == '\\') {
      esc = !esc;
    } else {
      if (*s == ' ' && !esc) {
        last = s + 1;
      }
      esc = false;
    }
  }
  lua_pushlstring(L, string, last - string);
  lua_pushstring(L, last);
  return 2;
}


static int l_fn_unquote_space(lua_State *L)
{
  const char *string = luaL_checkstring(L, 1);
  char *buf = malloc(strlen(string) + 1);
  char *t = buf;
  for (const char *s = string; *s != 0; s++) {
    if (*s != '\\' || *(s+1) != ' ') {
      *t++ = *s;
    }
  }
  lua_pushlstring(L, buf, t-buf);
  free(buf);
  return 1;
}


static int l_fn_quote_space(lua_State *L)
{
  const char *string = luaL_checkstring(L, 1);
  char *buf = malloc(strlen(string) * 2 + 1);
  char *t = buf;
  for (const char *s = string; *s; s++) {
    if (*s == ' ') {
      *t++ = '\\';
    }
    *t++ = *s;
  }
  lua_pushlstring(L, buf, t-buf);
  free(buf);
  return 1;
}


static int l_fn_getpid(lua_State *L)
{
  lua_pushinteger(L, getpid());
  return 1;
}


static int l_fn_getcwd(lua_State *L)
{
  const char *cwd = getcwd(NULL, 0);
  lua_pushstring(L, cwd ? cwd : "");
  return 1;
}


static int l_fn_getpwd(lua_State *L)
{
  const char *pwd = getenv("PWD");
  lua_pushstring(L, pwd ? pwd : "");
  return 1;
}


static const struct luaL_Reg fn_lib[] = {
  {"split_last", l_fn_split_last},
  {"quote_space", l_fn_quote_space},
  {"unquote_space", l_fn_unquote_space},
  {"tokenize", l_fn_tokenize},
  {"mime", l_fn_mime},
  {"getpid", l_fn_getpid},
  {"getcwd", l_fn_getcwd},
  {"getpwd", l_fn_getpwd},
  {NULL, NULL}};

/* }}} */

void lua_run_hook(lua_State *L, const char *hook)
{
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "run_hook");
  lua_pushstring(L, hook);
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
  }
}


void lua_eval(lua_State *L, const char *expr)
{
  log_debug("eval %s", expr);
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "eval");
  lua_pushstring(L, expr);
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "eval: %s", lua_tostring(L, -1));
  }
}

void lua_handle_key(lua_State *L, input_t in)
{
  if (in == CTRL('Q')) {
    lfm_quit(lfm);
    return;
  }
  const char *prefix = cmdline_prefix_get(&ui->cmdline);
  if (!maps.cur) {
    maps.cur = prefix ? maps.cmd : maps.normal;
    cvector_set_size(maps.seq, 0);
    maps.count = -1;
    maps.accept_count = true;
  }
  if (!prefix && maps.accept_count && '0' <= in && in <= '9') {
    if (maps.count < 0) {
      maps.count = in - '0';
    } else {
      maps.count = maps.count * 10 + in - '0';
    }
    if (maps.count > 0) {
      cvector_push_back(maps.seq, in);
      ui_keyseq_show(ui, maps.seq);
    }
    return;
  }
  maps.cur = trie_find_child(maps.cur, in);
  if (prefix) {
    if (!maps.cur) {
      if (iswprint(in)) {
        char buf[MB_LEN_MAX+1];
        int n = wctomb(buf, in);
        if (n < 0) {
          n = 0; // invalid character or borked shift/ctrl/alt
        }
        buf[n] = '\0';
        if (cmdline_insert(&ui->cmdline, buf)) {
          ui_redraw(ui, REDRAW_CMDLINE);
        }
      }
      lua_getglobal(L, "lfm");
      if (lua_type(L, -1) == LUA_TTABLE) {
        lua_getfield(L, -1, "modes");
        if (lua_type(L, -1) == LUA_TTABLE) {
          lua_getfield(L, -1, prefix);
          if (lua_type(L, -1) == LUA_TTABLE) {
            lua_getfield(L, -1, "on_change");
            if (lua_type(L, -1) == LUA_TFUNCTION) {
              lua_pcall(L, 0, 0, 0);
            }
          }
        }
      }
    } else {
      if (maps.cur->keys) {
        lua_pushlightuserdata(L, (void *) maps.cur);
        lua_gettable(L, LUA_REGISTRYINDEX);
        maps.cur = NULL;
        if (lua_pcall(L, 0, 0, 0)) {
          ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
        }
      }
    }
  } else {
    // prefix == NULL, i.e. normal mode
    if (in == NCKEY_ESC) {
      if (cvector_size(maps.seq) > 0) {
        // clear keys in the buffer
        maps.cur = NULL;
        ui_menu_hide(ui);
        ui_keyseq_hide(ui);
      } else {
        // clear selection etc
        // TODO: this should be done properly with modes (on 2022-02-13)
        nohighlight(ui);
        fm_selection_visual_stop(fm);
        fm_selection_clear(fm);
        fm_paste_buffer_clear(fm);
        lua_run_hook(L, LFM_HOOK_PASTEBUF);
      }
      ui->message = false;
      ui_redraw(ui, REDRAW_FM);
    } else if (!maps.cur) {
      // no keymapping, print an error
      cvector_push_back(maps.seq, in);
      char *str = NULL;
      for (size_t i = 0; i < cvector_size(maps.seq); i++) {
        for (const char *s = input_to_key_name(maps.seq[i]); *s; s++) {
          cvector_push_back(str, *s);
        }
      }
      cvector_push_back(str, 0);
      log_debug("key: %d, id: %d, shift: %d, ctrl: %d alt %d, %s",
          in, ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in), str);
      cvector_free(str);
      ui_menu_hide(ui);
      ui_keyseq_hide(ui);
    } else if (maps.cur->keys) {
      // A command is mapped to the current keysequence. Execute it and reset.
      ui_menu_hide(ui);
      lua_pushlightuserdata(L, (void *) maps.cur);
      lua_gettable(L, LUA_REGISTRYINDEX);
      maps.cur = NULL;
      ui_keyseq_hide(ui);
      int nargs = 0;
      if (maps.count > 0) {
        lua_pushnumber(L, maps.count);
        nargs++;
      }
      if (lua_pcall(L, nargs, 0, 0)) {
        ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
      }
    } else {
      cvector_push_back(maps.seq, in);
      ui_keyseq_show(ui, maps.seq);
      maps.accept_count = false;

      Trie **leaves = NULL;
      trie_collect_leaves(maps.cur, &leaves, true);

      char **menu = NULL;

      cvector_push_back(menu, strdup("\033[1mkeys\tcommand\033[0m"));
      char *s;
      for (size_t i = 0; i < cvector_size(leaves); i++) {
        asprintf(&s, "%s\t%s", leaves[i]->keys, leaves[i]->desc ? leaves[i]->desc : "");
        cvector_push_back(menu, s);
      }
      cvector_free(leaves);
      ui_menu_show(ui, menu);
    }
  }
}


bool lua_load_file(lua_State *L, const char *path)
{
  if (luaL_loadfile(L, path) || lua_pcall(L, 0, 0, 0)) {
    ui_error(ui, "loadfile : %s", lua_tostring(L, -1));
    return false;
  }
  return true;
}


int luaopen_lfm(lua_State *L)
{
  log_debug("opening lualfm libs");

  luaL_openlib(L, "lfm", lfm_lib, 1);

  lua_newtable(L); /* lfm.cfg */

  lua_newtable(L); /* lfm.cfg.colors */
  luaL_newmetatable(L, "colors_mt");
  luaL_register(L, NULL, colors_mt);
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "colors"); /* lfm.cfg.colors = {...} */

  luaL_newmetatable(L, "config_mt");
  luaL_register(L, NULL, config_mt);
  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "config"); /* lfm.config = {...} */

  lua_newtable(L); /* lfm.log */
  luaL_register(L, NULL, log_lib);
  lua_setfield(L, -2, "log"); /* lfm.log = {...} */

  lua_newtable(L); /* lfm.ui */
  luaL_register(L, NULL, ui_lib);
  lua_setfield(L, -2, "ui"); /* lfm.ui = {...} */

  lua_newtable(L); /* lfm.cmd */
  luaL_register(L, NULL, cmd_lib);
  lua_setfield(L, -2, "cmd"); /* lfm.cmd = {...} */

  lua_newtable(L); /* lfm.fm */
  luaL_register(L, NULL, fm_lib);
  lua_setfield(L, -2, "fm"); /* lfm.fm = {...} */

  lua_newtable(L); /* lfm.fn */
  luaL_register(L, NULL, fn_lib);
  lua_setfield(L, -2, "fn"); /* lfm.fn = {...} */

  lua_newtable(L); /* lfm.rifle */
  lua_register_rifle(L);
  lua_setfield(L, -2, "rifle"); /* lfm.rifle = {...} */

  return 1;
}


void lua_init(lua_State *L, Lfm *_lfm)
{
  lfm = _lfm;
  ui = &_lfm->ui;
  fm = &_lfm->fm;

  maps.normal = trie_create();
  maps.cmd = trie_create();
  maps.cur = NULL;
  maps.seq = NULL;

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  lua_newtable(L);

  lua_load_file(L, cfg.corepath);
}


void lua_deinit(lua_State *L)
{
  lua_rifle_clear(L);
  lua_close(L);
  trie_destroy(maps.normal);
  trie_destroy(maps.cmd);
  cvector_free(maps.seq);
}
