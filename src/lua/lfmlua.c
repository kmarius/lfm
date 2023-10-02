#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdint.h>
#include <string.h>

#include "../config.h"
#include "../log.h"
#include "lfm.h"
#include "private.h"

typedef struct {
  char *name;
  const uint8_t *data;
  size_t size;
} ModuleDef;

#include "lua/lfm_module.generated.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

Lfm *lfm = NULL;
Ui *ui = NULL;
Fm *fm = NULL;

static int llua_pcall(lua_State *lstate, int nargs, int nresults) {
  lua_getglobal(lstate, "debug");
  lua_getfield(lstate, -1, "traceback");
  lua_remove(lstate, -2);
  lua_insert(lstate, -2 - nargs);
  int status = lua_pcall(lstate, nargs, nresults, -2 - nargs);
  if (status) {
    lua_remove(lstate, -2);
  } else {
    lua_remove(lstate, -1 - nresults);
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

static inline bool llua_init_packages(lua_State *L) {
  // put builtin packages in preload
  lua_getglobal(L, "package");    // [package]
  lua_getfield(L, -1, "preload"); // [package, preload]
  for (size_t i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
    ModuleDef def = builtin_modules[i];
    lua_pushinteger(L, (long)i);                // [package, preload, i]
    lua_pushcclosure(L, l_module_preloader, 1); // [package, preload, cclosure]
    lua_setfield(L, -2, def.name);              // [package, preload]
  }

  lua_pop(L, 2); // []

  lua_getglobal(L, "require");
  lua_pushstring(L, "lfm._core");
  if (llua_pcall(L, 1, 0)) {
    ui_error(ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    false;
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

void llua_call_ref1(lua_State *L, int ref, const char *line) {
  lua_get_callback(L, ref, false); // [f]
  lua_pushstring(L, line);
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

void llua_run_stdout_callback(lua_State *L, int ref, const char *line) {
  // always call this, if line is NULL, we remove the callback from the
  // registry
  lua_get_callback(L, ref, line == NULL); // [f]
  if (!line) {
    lua_pop(L, 1); // []
    return;
  }
  lua_pushstring(L, line);                   // [f, line]
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

void llua_eval(lua_State *L, const char *expr) {
  log_debug("lua_eval %s", expr);
  lua_getglobal(L, "lfm");     // [lfm]
  lua_getfield(L, -1, "eval"); // [lfm, lfm.eval]
  lua_pushstring(L, expr);     // [lfm, lfm.eval, expr]
  if (llua_pcall(L, 1, 0)) {   // [lfm]
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

void llua_init(lua_State *L, Lfm *lfm_) {
  lfm = lfm_;
  ui = &lfm_->ui;
  fm = &lfm_->fm;

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  llua_init_packages(L);

  uint64_t t0 = current_micros();
  if (cfg.user_configpath) {
    llua_load_file(L, cfg.user_configpath, true);
  } else {
    llua_load_file(L, cfg.configpath, false);
  }
  log_info("user configuration loaded in %.2fms",
           (current_micros() - t0) / 1000.0);
}

void llua_deinit(lua_State *L) {
  lua_close(L);
}
