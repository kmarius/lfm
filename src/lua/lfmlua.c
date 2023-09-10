#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdint.h>

#include "../config.h"
#include "../log.h"
#include "internal.h"
#include "lfmlib.h"

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
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "loadfile: %s", lua_tostring(L, -1));
    false;
  }

  return true;
}

void llua_run_callback(lua_State *L, int ref) {
  if (lua_get_callback(L, ref, true)) { // [elem]
    if (lua_pcall(L, 0, 0, 0)) {        // []
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_call_ref(lua_State *L, int ref) {
  if (lua_get_callback(L, ref, false)) { // [elem]
    if (lua_pcall(L, 0, 0, 0)) {         // []
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_call_ref1(lua_State *L, int ref, const char *line) {
  if (lua_get_callback(L, ref, false)) { // [elem]
    lua_pushstring(L, line);
    if (lua_pcall(L, 1, 0, 0)) { // []
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_run_child_callback(lua_State *L, int ref, int rstatus) {
  if (lua_get_callback(L, ref, true)) { // [elem]
    lua_pushnumber(L, rstatus);         // [elem, rstatus]
    if (lua_pcall(L, 1, 0, 0)) {        // []
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_run_stdout_callback(lua_State *L, int ref, const char *line) {
  if (lua_get_callback(L, ref, line == NULL)) { // [elem]
    if (line) {
      lua_pushstring(L, line);     // [elem, line]
      if (lua_pcall(L, 1, 0, 0)) { // []
        ui_error(ui, "cb: %s", lua_tostring(L, -1));
      }
    } else {
      lua_pop(L, 1);
    }
  }
}

void llua_run_hook(lua_State *L, const char *hook) {
  lua_getglobal(L, "lfm");         // [lfm]
  lua_getfield(L, -1, "run_hook"); // [lfm, lfm.run_hook]
  if (lua_isnoneornil(L, -1)) {
    log_error("lfm.run_hook is nil");
  }
  lua_pushstring(L, hook);     // [lfm, lfm.run_hook, hook]
  if (lua_pcall(L, 1, 0, 0)) { // [lfm]
    ui_error(ui, "run_hook(%s): %s", hook, lua_tostring(L, -1));
  }
  lua_pop(L, 1); // []
}

void llua_run_hook1(lua_State *L, const char *hook, const char *arg1) {
  lua_getglobal(L, "lfm");         // [lfm]
  lua_getfield(L, -1, "run_hook"); // [lfm, lfm.run_hook]
  lua_pushstring(L, hook);         // [lfm, lfm.run_hook, hook]
  lua_pushstring(L, arg1);         // [lfm, lfm.run_hook, hook, arg1]
  if (lua_pcall(L, 2, 0, 0)) {     // [lfm]
    ui_error(ui, "run_hook(%s, %s): %s", hook, arg1, lua_tostring(L, -1));
  }
  lua_pop(L, 1); // []
}

void llua_call_from_ref(lua_State *L, int ref, int count) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [elem]
  if (count > 0) {
    lua_pushnumber(L, count); // [elem, count]
  }
  if (lua_pcall(L, count > 0 ? 1 : 0, 0, 0)) { // []
    ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
  }
}

void llua_eval(lua_State *L, const char *expr) {
  log_debug("lua_eval %s", expr);
  lua_getglobal(L, "lfm");     // [lfm]
  lua_getfield(L, -1, "eval"); // [lfm, lfm.eval]
  lua_pushstring(L, expr);     // [lfm, lfm.eval, expr]
  if (lua_pcall(L, 1, 0, 0)) { // [lfm]
    ui_error(ui, "eval: %s", lua_tostring(L, -1));
  }
  lua_pop(L, 1);
}

bool llua_load_file(lua_State *L, const char *path, bool err_on_non_exist) {
  if (luaL_loadfile(L, path)) {
    if (!err_on_non_exist) {
      ui_error(ui, "loadfile: %s", lua_tostring(L, -1));
    }
    return false;
  }
  if (lua_pcall(L, 0, 0, 0)) {
    ui_error(ui, "loadfile: %s", lua_tostring(L, -1));
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
  llua_load_file(L, cfg.configpath, true);
  log_info("user configuration loaded in %.2fms",
           (current_micros() - t0) / 1000.0);
}

void llua_deinit(lua_State *L) {
  lua_close(L);
}
