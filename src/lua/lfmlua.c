#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "internal.h"
#include "lfmlib.h"
#include "../config.h"
#include "../log.h"

Lfm *lfm = NULL;
Ui *ui = NULL;
Fm *fm = NULL;

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
      ui_error(ui, "cb: %s", lua_tostring(L, -1));
    }
  }
}

void lua_run_hook(lua_State *L, const char *hook)
{
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "run_hook");
  lua_pushstring(L, hook);
  if (lua_pcall(L, 1, 0, 0)) {
    ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
  }
}

void lua_run_hook1(lua_State *L, const char *hook, const char* arg1)
{
  lua_getglobal(L, "lfm");
  lua_getfield(L, -1, "run_hook");
  lua_pushstring(L, hook);
  lua_pushstring(L, arg1);
  if (lua_pcall(L, 2, 0, 0)) {
    ui_error(ui, "run_hook: %s", lua_tostring(L, -1));
  }
}

void lua_call_from_ref(lua_State *L, int ref, int count)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (count > 0) {
    lua_pushnumber(L, count);
  }
  if (lua_pcall(L, count > 0 ? 1 : 0, 0, 0)) {
    ui_error(ui, "handle_key: %s", lua_tostring(L, -1));
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

bool lua_load_file(lua_State *L, const char *path)
{
  if (luaL_loadfile(L, path) || lua_pcall(L, 0, 0, 0)) {
    ui_error(ui, "loadfile: %s", lua_tostring(L, -1));
    return false;
  }
  return true;
}

void lua_call_on_change(lua_State *L, const char *prefix)
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

void lua_init(lua_State *L, Lfm *_lfm)
{
  lfm = _lfm;
  ui = &_lfm->ui;
  fm = &_lfm->fm;

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  lua_load_file(L, cfg.corepath);
}

void lua_deinit(lua_State *L)
{
  lua_close(L);
}
