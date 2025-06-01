#include "lfmlua.h"

#include "../config.h"
#include "../log.h"
#include "../profiling.h"
#include "lfm.h"
#include "private.h"

#include <lauxlib.h>
#include <linux/limits.h>
#include <lua.h>
#include <lualib.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *name;
  const uint8_t *data;
  size_t size;
} ModuleDef;

#include "lua/lfm_module.generated.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

Lfm *lfm = NULL;
Ui *ui = NULL;
Fm *fm = NULL;

int llua_pcall(lua_State *L, int nargs, int nresults) {
  lua_getglobal(L, "debug");
  lua_getfield(L, -1, "traceback");
  lua_remove(L, -2);
  lua_insert(L, -2 - nargs);

  int status = lua_pcall(L, nargs, nresults, -2 - nargs);

  if (status != LUA_OK) {
    lua_remove(L, -2);
  } else {
    lua_remove(L, -1 - nresults);
  }
  return status;
}

// package preloading borrowed from neovim

static int l_module_preloader(lua_State *L) {
  size_t i = (size_t)lua_tointeger(L, lua_upvalueindex(1));
  ModuleDef def = builtin_modules[i];
  char name[256];
  name[0] = '@';
  size_t off = xstrlcpy(name + 1, def.name, (sizeof name) - 2);
  strchrsub(name + 1, '.', '/');
  xstrlcpy(name + 1 + off, ".lua", (sizeof name) - 2 - off);

  if (luaL_loadbuffer(L, (const char *)def.data, def.size - 1, name)) {
    return lua_error(L);
  }

  lua_call(L, 0, 1); // propagates error to caller
  return 1;
}

static inline int is_package_loaded(lua_State *L, const char *name) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "loaded");
  lua_getfield(L, -1, name);
  bool loaded = !lua_isnil(L, -1);
  lua_pop(L, 3);
  return loaded;
}

static int l_require(lua_State *L) {
  // not entirely sure if we need to completely mimic the original require
  const char *name = luaL_checkstring(L, 1);
  if (is_package_loaded(L, name)) {
    lua_getglobal(L, "_require");
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
  } else {
    PROFILE(strdup(name), {
      lua_getglobal(L, "_require");
      lua_pushvalue(L, 1);
      lua_call(L, 1, 1);
    })
  }
  return 1;
}

static inline bool llua_init_packages(lua_State *L) {
  // put builtin packages in preload
  lua_getglobal(L, "package");    // [package]
  lua_getfield(L, -1, "preload"); // [package, preload]
  for (size_t i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
    ModuleDef def = builtin_modules[i];
    lua_pushinteger(L, (long)i); // [package, preload, i]
    lua_pushcclosure(L, l_module_preloader,
                     1);           // [package, preload, cclosure]
    lua_setfield(L, -2, def.name); // [package, preload]
  }

  lua_pop(L, 2); // []

  // override require for profiling
  lua_getglobal(L, "require");     // [require]
  lua_setglobal(L, "_require");    // []
  lua_pushcfunction(L, l_require); // [l_require]
  lua_setglobal(L, "require");     // []

  lua_getglobal(L, "require");
  lua_pushstring(L, "lfm._core");
  if (llua_pcall(L, 1, 0)) {
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }

  return true;
}

void llua_run_callback(lua_State *L, int ref) {
  lua_get_callback(L, ref, true); // [f]
  if (llua_pcall(L, 0, 0)) {      // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void llua_call_ref(lua_State *L, int ref) {
  lua_get_callback(L, ref, false); // [f]
  if (llua_pcall(L, 0, 0)) {       // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void llua_call_ref1(lua_State *L, int ref, zsview line) {
  lua_get_callback(L, ref, false); // [f]
  lua_pushzsview(L, line);
  if (llua_pcall(L, 1, 0)) { // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void llua_run_child_callback(lua_State *L, int ref, int rstatus) {
  lua_get_callback(L, ref, true); // [f]
  lua_pushnumber(L, rstatus);     // [f, rstatus]
  if (llua_pcall(L, 1, 0)) {      // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

// line does not have to be nul terminated, in this case, len hase to be
// passed; len is negative, strlen(line) is used
void llua_run_stdout_callback(lua_State *L, int ref, const char *line,
                              ssize_t len) {
  // always call this, if line is NULL, we remove the callback from the
  // registry
  lua_get_callback(L, ref, line == NULL); // [f]
  if (!line) {
    lua_pop(L, 1); // []
    return;
  }

  if (len < 0)
    len = strlen(line);

  lua_pushlstring(L, line, len);             // [f, line]
  if (llua_pcall(L, 1, 00)) {                // []
    ui_error(ui, "%s", lua_tostring(L, -1)); // [err]
    lua_pop(L, 1);                           // []
  }
}

void llua_call_from_ref(lua_State *L, int ref, int count) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [f]
  if (count > 0) {
    lua_pushnumber(L, count); // [f, count]
  }
  if (llua_pcall(L, count > 0 ? 1 : 0, 0)) { // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void llua_evaln(lua_State *L, const char *expr, int len) {
  log_debug("lua_eval %s", expr);
  lua_getglobal(L, "lfm");       // [lfm]
  lua_getfield(L, -1, "eval");   // [lfm, lfm.eval]
  lua_pushlstring(L, expr, len); // [lfm, lfm.eval, expr]
  if (llua_pcall(L, 1, 0)) {     // [lfm]
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

bool llua_load_file(lua_State *L, const char *path, bool err_on_non_exist) {
  if (luaL_loadfile(L, path)) {
    if (err_on_non_exist ||
        !strstr(lua_tostring(L, -1), "No such file or directory")) {
      ui_error(ui, "%s", lua_tostring(L, -1));
    }
    return false;
  }
  if (llua_pcall(L, 0, 0)) {
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

void lfm_lua_init_thread(lua_State *L) {

  lua_newtable(L);

  luaopen_log(L);
  lua_setfield(L, -2, "log");

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");

  for (size_t i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
    ModuleDef def = builtin_modules[i];
    if (streq(def.name, "lfm.fs")) {
      if (luaL_loadbuffer(L, (const char *)def.data, def.size - 1, def.name)) {
        log_error("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
      }
      if (lua_pcall(L, 0, 1, 0)) {
        log_error("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
      }
      lua_setfield(L, -2, "fs");
      break;
    }
  }

  lua_setglobal(L, "lfm");

  if (luaL_dostring(L, "lfm.validate = function() end")) {
    log_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lfm_lua_init(Lfm *lfm_) {
  lfm = lfm_;
  ui = &lfm_->ui;
  fm = &lfm_->fm;

  lua_State *L = luaL_newstate();
  lfm->L = L;
  ev_set_userdata(lfm->loop, lfm->L);

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  set_package_path(L);
  llua_init_packages(L);

  PROFILE("user_config", {
    if (lfm->opts.config) {
      llua_load_file(L, lfm->opts.config, true);
    } else {
      llua_load_file(L, cstr_str(&cfg.configpath), false);
    }
  });
}

void lfm_lua_deinit(Lfm *lfm) {
  lua_close(lfm->L);
  lfm->L = NULL;
}

bool llua_filter(lua_State *L, int ref, const char *name) {
  lua_get_callback(L, ref, false);
  lua_pushstring(L, name);
  if (llua_pcall(L, 1, 1)) { // []
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return lua_toboolean(L, -1);
}
