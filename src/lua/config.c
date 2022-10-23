#include <lauxlib.h>

#include "internal.h"
#include "../config.h"
#include "../log.h"
#include "../ncutil.h"
#include "../tpool.h"

#define DIR_SETTINGS_META "dir_settings_mt"
#define CONFIG_META "config_mt"
#define COLORS_META "colors_mt"

static inline int lua_dir_settings_set(lua_State *L, const char *path, int ind)
{
  if (lua_isnil(L, ind)) {
    ht_delete(cfg.dir_settings_map, path);
    Dir *d = ht_get(lfm->loader.dir_cache, path);
    if (d) {
      memcpy(&d->settings, &cfg.dir_settings, sizeof d->settings);
    }
    return 0;
  }

  luaL_checktype(L, ind, LUA_TTABLE);

  struct dir_settings s;
  memcpy(&s, &cfg.dir_settings, sizeof s);

  lua_getfield(L, ind, "sorttype");
  if (!lua_isnil(L, -1)) {
    const char *op = luaL_checkstring(L, -1);
    if (streq(op, "name")) {
      s.sorttype = SORT_NAME;
    } else if (streq(op, "natural")) {
      s.sorttype = SORT_NATURAL;
    } else if (streq(op, "ctime")) {
      s.sorttype = SORT_CTIME;
    } else if (streq(op, "size")) {
      s.sorttype = SORT_SIZE;
    } else if (streq(op, "random")) {
      s.sorttype = SORT_RAND;
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, ind, "dirfirst");
  if (!lua_isnil(L, -1)) {
    s.dirfirst = lua_toboolean(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, ind, "reverse");
  if (!lua_isnil(L, -1)) {
    s.reverse = lua_toboolean(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, ind, "hidden");
  // this is probably not applied correctly because it essentially
  // treated as a global setting via cfg.dir_settings
  if (!lua_isnil(L, -1)) {
    s.hidden = lua_toboolean(L, -1);
  }
  lua_pop(L, 1);

  config_dir_setting_add(path, &s);
  Dir *d = ht_get(lfm->loader.dir_cache, path);
  if (d) {
    memcpy(&d->settings, &s, sizeof s);
  }

  return 0;
}

static int l_dir_settings_index(lua_State *L)
{
  const char *key = luaL_checkstring(L, 2);
  struct dir_settings *s = ht_get(cfg.dir_settings_map, key);
  if (s) {
    lua_newtable(L);
    lua_pushboolean(L, s->dirfirst);
    lua_setfield(L, -2, "dirfirst");
    lua_pushboolean(L, s->hidden);
    lua_setfield(L, -2, "hidden");
    lua_pushboolean(L, s->reverse);
    lua_setfield(L, -2, "reverse");
    /* TODO: refactor this if we need to convert enum <-> string more often (on 2022-10-09) */
    switch (s->sorttype) {
      case SORT_NATURAL:
        lua_pushstring(L, "natural"); break;
      case SORT_NAME:
        lua_pushstring(L, "name"); break;
      case SORT_SIZE:
        lua_pushstring(L, "size"); break;
      case SORT_CTIME:
        lua_pushstring(L, "ctime"); break;
      case SORT_RAND:
        lua_pushstring(L, "random"); break;
      default:
        lua_pushstring(L, "unknown"); break;
    }
    lua_setfield(L, -2, "sorttype");
    return 1;
  } else {
    return 0;
  }
}

static int l_dir_settings_newindex(lua_State *L)
{
  lua_dir_settings_set(L, luaL_checkstring(L, 2), 3);
  return 0;
}

static const struct luaL_Reg dir_settings_mt[] = {
  {"__index", l_dir_settings_index},
  {"__newindex", l_dir_settings_newindex},
  {NULL, NULL}};

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
    lua_pushboolean(L, cfg.dir_settings.hidden);
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
  } else if (streq(key, "previewer")) {
    lua_pushstring(L, cfg.previewer ? cfg.previewer : "");
    return 1;
  } else if (streq(key, "icons")) {
    lua_pushboolean(L, cfg.icons);
    return 1;
  } else if (streq(key, "icon_map")) {
    lua_createtable(L, 0, cfg.icon_map->size);
    ht_foreach_kv(const char *key, const char *val, cfg.icon_map) {
      lua_pushstring(L, val);
      lua_setfield(L, -2, key);
    }
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
  } else if (streq(key, "statedir")) {
    lua_pushstring(L, cfg.statedir);
    return 1;
  } else if (streq(key, "runtime_dir")) {
    lua_pushstring(L, cfg.rundir);
    return 1;
  } else if (streq(key, "dir_settings")) {
    lua_newtable(L);
    luaL_newmetatable(L, DIR_SETTINGS_META);
    lua_setmetatable(L, -2);
    return 1;
  } else if (streq(key, "threads")) {
    lua_pushnumber(L, tpool_size(lfm->async.tpool));
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
    luaL_checktype(L, 3, LUA_TTABLE);
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
    luaL_checktype(L, 3, LUA_TTABLE);
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
  } else if (streq(key, "icons")) {
    cfg.icons = lua_toboolean(L, 3);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "icon_map")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    ht_clear(cfg.icon_map);
    for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
      config_icon_map_add(lua_tostring(L, -2), lua_tostring(L, -1));
    }
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "dir_settings")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    ht_clear(cfg.dir_settings_map);
    for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
      lua_dir_settings_set(L, luaL_checkstring(L, -2), -1);
    }
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
  } else if (streq(key, "threads")) {
    const int num = luaL_checknumber(L, 3);
    luaL_argcheck(L, num >= 2, 3, "argument must be at least 2");
    tpool_resize(lfm->async.tpool, num);
    return 0;
  } else if (streq(key, "infoline")) {
    if (lua_isnil(L, 3)) {
      ui_set_infoline(&lfm->ui, NULL);
    } else {
      ui_set_infoline(&lfm->ui, luaL_checkstring(L, 3));
    }
  } else {
    luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}

static const struct luaL_Reg config_mt[] = {
  {"__index", l_config_index},
  {"__newindex", l_config_newindex},
  {NULL, NULL}};

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
          config_color_map_add(lua_tostring(L, -1), ch);
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

int luaopen_config(lua_State *L)
{
  luaL_newmetatable(L, DIR_SETTINGS_META);
  luaL_register(L, NULL, dir_settings_mt);
  lua_pop(L, 1);

  lua_newtable(L);

  lua_newtable(L);
  luaL_newmetatable(L, COLORS_META);
  luaL_register(L, NULL, colors_mt);
  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "colors");

  luaL_newmetatable(L, CONFIG_META);
  luaL_register(L, NULL, config_mt);
  lua_setmetatable(L, -2);

  return 1;
}
