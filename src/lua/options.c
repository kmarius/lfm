#include "../config.h"
#include "../infoline.h"
#include "../ncutil.h"
#include "../path.h"
#include "../stc/cstr.h"
#include "../tpool.h"
#include "lua.h"
#include "private.h"
#include "util.h"

#include <lauxlib.h>

#include <stdint.h>
#include <stdlib.h>

#define DIRSETTINGS_META "Lfm.Dirsettings.Meta"
#define OPTIONS_META "Lfm.Config.Meta"
#define COLORS_META "Lfm.Colors.Meta"

static inline int llua_dir_settings_set(lua_State *L, zsview path, int ind) {

  if (lua_isnil(L, ind)) {
    hmap_dirsetting_erase(&cfg.dir_settings_map, path);
    dircache_value *v = dircache_get_mut(&lfm->loader.dc, path);
    if (v) {
      memcpy(&v->second->settings, &cfg.dir_settings,
             sizeof v->second->settings);
    }
    return 0;
  }

  luaL_checktype(L, ind, LUA_TTABLE);

  struct dir_settings s;
  memcpy(&s, &cfg.dir_settings, sizeof s);

  lua_getfield(L, ind, "sorttype");
  if (!lua_isnil(L, -1)) {
    const char *op = luaL_checkstring(L, -1);
    int type = sorttype_from_str(op);
    if (type < 0) {
      return luaL_error(L, "unrecognized sort type: %s", op);
    }
    s.sorttype = type;
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

  hmap_dirsetting_emplace_or_assign(&cfg.dir_settings_map, path, s);

  dircache_value *v = dircache_get_mut(&lfm->loader.dc, path);
  if (v) {
    memcpy(&v->second->settings, &s, sizeof s);
  }

  return 0;
}

static int l_dir_settings_index(lua_State *L) {
  zsview key = luaL_checkzsview(L, 2);
  const hmap_dirsetting_value *v =
      hmap_dirsetting_get(&cfg.dir_settings_map, key);
  if (v == NULL) {
    return 0;
  }
  const struct dir_settings *s = &v->second;

  lua_createtable(L, 0, 5);
  lua_pushboolean(L, s->dirfirst);
  lua_setfield(L, -2, "dirfirst");
  lua_pushboolean(L, s->hidden);
  lua_setfield(L, -2, "hidden");
  lua_pushboolean(L, s->reverse);
  lua_setfield(L, -2, "reverse");
  lua_pushstring(L, fileinfo_str[s->fileinfo]);
  lua_setfield(L, -2, "info");
  lua_pushstring(L, sorttype_str[s->sorttype]);
  lua_setfield(L, -2, "sorttype");

  return 1;
}

static int l_dir_settings_newindex(lua_State *L) {
  llua_dir_settings_set(L, luaL_checkzsview(L, 2), 3);
  return 0;
}

static const struct luaL_Reg dir_settings_mt[] = {
    {"__index",    l_dir_settings_index   },
    {"__newindex", l_dir_settings_newindex},
    {NULL,         NULL                   },
};

static int l_config_index(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "truncatechar")) {
    lua_pushstring(L, cfg.truncatechar);
    return 1;
  } else if (streq(key, "hidden")) {
    lua_pushboolean(L, cfg.dir_settings.hidden);
    return 1;
  } else if (streq(key, "ratios")) {
    const size_t l = vec_int_size(&cfg.ratios);
    lua_createtable(L, l, 0);
    for (size_t i = 0; i < l; i++) {
      lua_pushinteger(L, *vec_int_at(&cfg.ratios, i));
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  } else if (streq(key, "inotify_blacklist")) {
    lua_push_vec_cstr(L, &cfg.inotify_blacklist);
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
    lua_pushcstr(L, &cfg.previewer);
    return 1;
  } else if (streq(key, "icons")) {
    lua_pushboolean(L, cfg.icons);
    return 1;
  } else if (streq(key, "icon_map")) {
    lua_createtable(L, 0, hmap_icon_size(&cfg.icon_map));
    c_foreach_kv(k, v, hmap_icon, cfg.icon_map) {
      lua_pushcstr(L, v);
      lua_setfield(L, -2, cstr_str(k));
    }
    return 1;
  } else if (streq(key, "dir_settings")) {
    lua_newtable(L);
    luaL_newmetatable(L, DIRSETTINGS_META);
    lua_setmetatable(L, -2);
    return 1;
  } else if (streq(key, "threads")) {
    lua_pushnumber(L, tpool_size(lfm->async.tpool));
    return 1;
  } else if (streq(key, "infoline")) {
    lua_pushcstr(L, &cfg.infoline);
    return 1;
  } else if (streq(key, "histsize")) {
    lua_pushnumber(L, cfg.histsize);
    return 1;
  } else if (streq(key, "map_suggestion_delay")) {
    lua_pushnumber(L, cfg.map_suggestion_delay);
    return 1;
  } else if (streq(key, "map_clear_delay")) {
    lua_pushnumber(L, cfg.map_clear_delay);
    return 1;
  } else if (streq(key, "loading_indicator_delay")) {
    lua_pushnumber(L, cfg.loading_indicator_delay);
    return 1;
  } else if (streq(key, "linkchars")) {
    lua_pushstring(L, cfg.linkchars);
    return 1;
  } else if (streq(key, "timefmt")) {
    lua_pushcstr(L, &cfg.timefmt);
    return 1;
  } else if (streq(key, "preview_delay")) {
    lua_pushnumber(L, cfg.preview_delay);
    return 1;
  } else if (streq(key, "tags")) {
    lua_pushboolean(L, cfg.tags);
    return 1;
  } else if (streq(key, "mapleader")) {
    lua_pushstring(L, input_to_key_name(cfg.mapleader, NULL));
    return 1;
  } else {
    luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}

static int l_config_newindex(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "truncatechar")) {
    size_t len;
    const char *val = luaL_checklstring(L, 3, &len);
    if (len > 0) {
      len = utf8_chr_size(val);
      memcpy(cfg.truncatechar, val, len);
    }
    cfg.truncatechar[len] = 0;
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "hidden")) {
    fm_hidden_set(fm, lua_toboolean(L, 3));
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "ratios")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    const size_t l = lua_objlen(L, 3);
    if (l == 0) {
      luaL_argerror(L, 3, "no ratios given");
    }
    vec_int ratios = vec_int_init();
    for (uint32_t i = 1; i <= l; i++) {
      lua_rawgeti(L, 3, i);
      int32_t val = lua_tointeger(L, -1);
      if (val <= 0) {
        vec_int_drop(&ratios);
        return luaL_error(L, "ratio must be non-negative");
      }
      vec_int_push(&ratios, lua_tointeger(L, -1));
      lua_pop(L, 1);
    }
    vec_int_drop(&cfg.ratios);
    cfg.ratios = ratios;
    fm_recol(fm);
    ui_recol(ui);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "inotify_blacklist")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_read_vec_cstr(L, 3, &cfg.inotify_blacklist);
  } else if (streq(key, "inotify_timeout")) {
    int n = luaL_checkinteger(L, 3);
    if (n < 100) {
      luaL_argerror(L, 3, "timeout must be larger than 100");
    }
    cfg.inotify_timeout = n;
    loader_reschedule(&lfm->loader);
  } else if (streq(key, "inotify_delay")) {
    int n = luaL_checkinteger(L, 3);
    luaL_argcheck(L, 3, n >= 0, "inotify_delay must be non-negative");
    cfg.inotify_delay = n;
    loader_reschedule(&lfm->loader);
  } else if (streq(key, "scrolloff")) {
    int n = luaL_checkinteger(L, 3);
    luaL_argcheck(L, 3, n >= 0, "scrolloff must be non-negative");
    cfg.scrolloff = n;
  } else if (streq(key, "preview")) {
    cfg.preview = lua_toboolean(L, 3);
    if (!cfg.preview) {
      ui_drop_cache(ui);
    }
    fm_recol(fm);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "preview_images")) {
    bool preview_images = lua_toboolean(L, 3);
    if (preview_images != cfg.preview_images) {
      cfg.preview_images = preview_images;
      fm_recol(fm);
      ui_drop_cache(ui);
      ui_redraw(ui, REDRAW_PREVIEW);
    }
  } else if (streq(key, "icons")) {
    cfg.icons = lua_toboolean(L, 3);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "icon_map")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    hmap_icon_clear(&cfg.icon_map);
    for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
      if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
        return luaL_error(L, "icon_map: non-string key/value found");
      }
      hmap_icon_emplace_or_assign(&cfg.icon_map, lua_tostring(L, -2),
                                  lua_tostring(L, -1));
    }
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "dir_settings")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    hmap_dirsetting_clear(&cfg.dir_settings_map);
    for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
      llua_dir_settings_set(L, luaL_checkzsview(L, -2), -1);
    }
  } else if (streq(key, "previewer")) {
    if (lua_isnoneornil(L, 3)) {
      cstr_clear(&cfg.previewer);
    } else {
      zsview str = luaL_checkzsview(L, 3);
      if (zsview_is_empty(str)) {
        cstr_clear(&cfg.previewer);
      } else {
        cstr path = path_replace_tilde(str);
        cstr_take(&cfg.previewer, path);
      }
    }
    ui_drop_cache(ui);
  } else if (streq(key, "lua_previewer")) {
    if (lua_isnoneornil(L, 3)) {
      cstr_clear(&cfg.previewer);
    } else {
      bytes chunk = lua_tobytes(L, 3);
      if (bytes_is_empty(chunk)) {
        cstr_clear(&cfg.previewer);
      } else {
        bytes_drop(&cfg.lua_previewer);
        cfg.lua_previewer = chunk;
      }
    }
    ui_drop_cache(ui);
  } else if (streq(key, "threads")) {
    const int num = luaL_checknumber(L, 3);
    luaL_argcheck(L, num >= 2, 3, "threads must be at least 2");
    tpool_resize(lfm->async.tpool, num);
  } else if (streq(key, "infoline")) {
    zsview line = lua_tozsview(L, 3);
    cstr_assign_zv(&cfg.infoline, line);
    infoline_parse(line);
    ui_redraw(ui, REDRAW_INFO);
  } else if (streq(key, "histsize")) {
    int sz = luaL_checkinteger(L, 3);
    luaL_argcheck(L, sz >= 0, 3, "histsize must be non-negative");
    cfg.histsize = sz;
  } else if (streq(key, "map_suggestion_delay")) {
    int delay = luaL_checkinteger(L, 3);
    luaL_argcheck(L, delay >= 0, 3,
                  "map_suggestion_delay must be non-negative");
    cfg.map_suggestion_delay = delay;
  } else if (streq(key, "map_clear_delay")) {
    int delay = luaL_checkinteger(L, 3);
    luaL_argcheck(L, delay >= 0, 3, "map_clear_delay must be non-negative");
    cfg.map_clear_delay = delay;
  } else if (streq(key, "loading_indicator_delay")) {
    int delay = luaL_checkinteger(L, 3);
    luaL_argcheck(L, delay >= 0, 3,
                  "loading_indicator_delay must be non-negative");
    cfg.loading_indicator_delay = delay;
  } else if (streq(key, "linkchars")) {
    size_t len;
    const char *val = luaL_checklstring(L, 3, &len);
    if (len > sizeof cfg.linkchars - 1) {
      return luaL_error(L, "linkchars too long");
    }
    strncpy(cfg.linkchars, val, sizeof(cfg.linkchars) - 1);
    cfg.linkchars_len = ansi_mblen(val);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "timefmt")) {
    zsview fmt = luaL_checkzsview(L, 3);
    cstr_assign_zv(&cfg.timefmt, fmt);
    ui_redraw(ui, REDRAW_FM);
  } else if (streq(key, "preview_delay")) {
    int delay = luaL_checkinteger(L, 3);
    luaL_argcheck(L, delay >= 0, 3, "preview_delay must be non-negative");
    cfg.preview_delay = delay;
    lfm->fm.cursor_resting_timer.repeat = delay / 1000.0;
    lfm->ui.preview_load_timer.repeat = delay / 1000.0;
  } else if (streq(key, "tags")) {
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    bool val = lua_toboolean(L, 3);
    if (val != cfg.tags) {
      cfg.tags = val;
      ui_redraw(ui, REDRAW_FM);
    }
  } else if (streq(key, "mapleader")) {
    input_t key;
    if (key_name_to_input(luaL_checkstring(L, 3), &key) < 0) {
      return luaL_error(L, "invalid key");
    }
    cfg.mapleader = key;
  } else {
    return luaL_error(L, "unexpected key %s", key);
  }
  return 0;
}

static const struct luaL_Reg options_mt[] = {
    {"__index",    l_config_index   },
    {"__newindex", l_config_newindex},
    {NULL,         NULL             },
};

static inline uint32_t read_channel(lua_State *L, int idx) {
  switch (lua_type(L, idx)) {
  case LUA_TSTRING:
    return NCCHANNEL_INITIALIZER_PALINDEX(lua_tointeger(L, idx));
  case LUA_TNUMBER:
    return NCCHANNEL_INITIALIZER_HEX(lua_tointeger(L, idx));
  default:
    luaL_typerror(L, idx, "string or number required");
    return 0;
  }
}

static inline uint64_t read_color_pair(lua_State *L, int ind) {
  uint32_t fg, bg = fg = 0;
  ncchannel_set_default(&fg);
  ncchannel_set_default(&bg);

  lua_getfield(L, ind, "fg");
  if (!lua_isnil(L, -1)) {
    fg = read_channel(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, ind, "bg");
  if (!lua_isnil(L, -1)) {
    bg = read_channel(L, -1);
  }
  lua_pop(L, 1);

  return ncchannels_combine(fg, bg);
}

static int l_colors_newindex(lua_State *L) {
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
          hmap_channel_emplace_or_assign(&cfg.colors.color_map,
                                         lua_tostring(L, -1), ch);
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
    {NULL,         NULL             },
};

int luaopen_options(lua_State *L) {
  luaL_newmetatable(L, DIRSETTINGS_META);
  luaL_register(L, NULL, dir_settings_mt);
  lua_pop(L, 1);

  lua_newtable(L);

  lua_newtable(L);
  luaL_newmetatable(L, COLORS_META);
  luaL_register(L, NULL, colors_mt);
  lua_setmetatable(L, -2);

  lua_setfield(L, -2, "colors");

  luaL_newmetatable(L, OPTIONS_META);
  luaL_register(L, NULL, options_mt);
  lua_setmetatable(L, -2);

  return 1;
}
