#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "internal.h"
#include "lfmlib.h"
#include "../config.h"
#include "../log.h"

typedef struct {
  char *name;
  const uint8_t *data;
  size_t size;
} ModuleDef;

#include "lua/lfm_module.generated.h"

#define ARRAY_SIZE(arr) (sizeof(arr)/ sizeof(arr[0]))

Lfm *lfm = NULL;
Ui *ui = NULL;
Fm *fm = NULL;

// package preloading borrowed from neovim

static int l_module_preloader(lua_State *L)
{
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

  lua_call(L, 0, 1);  // propagates error to caller
  return 1;
}

static inline bool llua_init_packages(lua_State *L)
{
  // put builtin packages in preload
  lua_getglobal(L, "package");  // [package]
  lua_getfield(L, -1, "preload");  // [package, preload]
  for (size_t i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
    ModuleDef def = builtin_modules[i];
    lua_pushinteger(L, (long)i);  // [package, preload, i]
    lua_pushcclosure(L, l_module_preloader, 1);  // [package, preload, cclosure]
    lua_setfield(L, -2, def.name);  // [package, preload]
  }

  lua_pop(L, 2);  // []

  lua_getglobal(L, "require");
  lua_pushstring(L, "lfm._core");
  if (lua_pcall(L, 1, 0, 0)) {
      ui_error(ui, "loadfile: %s", lua_tostring(L, -1));
      false;
  }

  return true;
}

void llua_run_callback(lua_State *L, int ref)
{
  if (lua_get_callback(L, ref, true)) {
    if (lua_pcall(L, 0, 0, 0)) {
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_run_child_callback(lua_State *L, int ref, int rstatus)
{
  if (lua_get_callback(L, ref, true)) {
    lua_pushnumber(L, rstatus);
    if (lua_pcall(L, 1, 0, 0)) {
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_run_stdout_callback(lua_State *L, int ref, const char *line)
{
  if (lua_get_callback(L, ref, line == NULL) && line) {
    lua_pushstring(L, line);
    lua_insert(L, -1);
    if (lua_pcall(L, 1, 0, 0)) {
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void llua_run_hook(lua_State *L, const char *hook)
{
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "run_hook");
  lua_pushstring(L, hook);
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
  }
}

void llua_run_hook1(lua_State *L, const char *hook, const char* arg1)
{
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "run_hook");
  lua_pushstring(L, hook);
  lua_pushstring(L, arg1);
  if (lua_pcall(L, 2, 0, 0)) {
    ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
  }
}

void llua_call_from_ref(lua_State *L, int ref, int count)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (count > 0) {
    lua_pushnumber(L, count);
  }
  if (lua_pcall(L, count > 0 ? 1 : 0, 0, 0)) {
    ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
  }
}

void llua_eval(lua_State *L, const char *expr)
{
  log_debug("lua_eval %s", expr);
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "eval");
  lua_pushstring(L, expr);
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "eval: %s", lua_tostring(L, -1));
  }
}

bool llua_load_file(lua_State *L, const char *path, bool err_on_non_exist)
{
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

void llua_call_on_change(lua_State *L, const char *prefix)
{
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
}

void llua_init(lua_State *L, Lfm *lfm_)
{
  lfm = lfm_;
  ui = &lfm_->ui;
  fm = &lfm_->fm;

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  llua_init_packages(L);

  llua_load_file(L, cfg.configpath, true);
}

void llua_deinit(lua_State *L)
{
  lua_close(L);
}
