#include "lfmlua.h"

#include "config.h"
#include "lfmlib.h"
#include "log.h"
#include "loop.h"
#include "private.h"
#include "profiling.h"
#include "stc/cstr.h"

#include <ev.h>
#include <lauxlib.h>
#include <linux/limits.h>
#include <lua.h>
#include <lualib.h>

#include <stdint.h>
#include <string.h>

typedef struct {
  const char *name;
  const u8 *data;
  usize size;
} ModuleDef;

#include "lua/lfm_module.generated.h"

static inline bool init_packages(lua_State *L);
static inline bool load_file(lua_State *L, const char *path,
                             bool err_on_non_exist);
#ifndef NDEBUG
static void check_lua_stack_cb(EV_P_ ev_check *w, i32 revents);
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

Lfm *lfm = NULL;
Ui *ui = NULL;
Fm *fm = NULL;

#define i_type modules
#define i_keypro cstr
#include "stc/hset.h"

// moduels that have been imported (and profiled)
static modules imported = {0};

void lfm_lua_init(Lfm *lfm_) {
  lfm = lfm_;
  ui = &lfm_->ui;
  fm = &lfm_->fm;

  lua_State *L = luaL_newstate();
  lfm->L = L;
  ev_set_userdata(event_loop, lfm->L);

  luaL_openlibs(L);
  luaopen_jit(L);
  luaopen_lfm(L);

  set_package_path(L);
  init_packages(L);

  PROFILE("user_config", {
    if (lfm->opts.config) {
      load_file(L, lfm->opts.config, true);
    } else {
      load_file(L, cstr_str(&cfg.configpath), false);
    }
  });

#ifndef NDEBUG
  ev_check_init(&lfm->check_lua_stack, check_lua_stack_cb);
  lfm->check_lua_stack.data = L;
  ev_check_start(event_loop, &lfm->check_lua_stack);
#endif
}

void lfm_lua_init_thread(lua_State *L) {
  lua_newtable(L);

  luaopen_log(L);
  lua_setfield(L, -2, "log");

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");

  lua_pushvalue(L, -1);
  lua_setglobal(L, "lfm");

  for (usize i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
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

  if (luaL_dostring(L, "lfm.validate = function() end")) {
    log_error("%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lfm_lua_deinit(Lfm *lfm) {
  lua_close(lfm->L);
  lfm->L = NULL;
  modules_drop(&imported);
}

#ifndef NDEBUG
// chack that we are not leaving stuff on the lua stack between iterations
static void check_lua_stack_cb(EV_P_ ev_check *w, i32 revents) {
  (void)revents;
  static i32 top = -1;
  i32 new_top = lua_gettop(w->data);
  if (top >= 0 && new_top != top)
    log_error("stack %d -> %d", top, new_top);
  top = new_top;
}
#endif

static inline bool load_file(lua_State *L, const char *path,
                             bool err_on_non_exist) {
  if (luaL_loadfile(L, path)) {
    if (err_on_non_exist ||
        !strstr(lua_tostring(L, -1), "No such file or directory")) {
      lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    }
    return false;
  }
  if (lfm_lua_pcall(L, 0, 0)) {
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

int lfm_lua_pcall(lua_State *L, int nargs, int nresults) {
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
  usize i = (usize)lua_tointeger(L, lua_upvalueindex(1));
  ModuleDef def = builtin_modules[i];
  char name[256];
  name[0] = '@';
  usize off = xstrlcpy(name + 1, def.name, (sizeof name) - 2);
  strchrsub(name + 1, '.', '/');
  xstrlcpy(name + 1 + off, ".lua", (sizeof name) - 2 - off);

  if (luaL_loadbuffer(L, (const char *)def.data, def.size - 1, name))
    return lua_error(L);

  lua_call(L, 0, 1); // propagates error to caller
  return 1;
}

static int l_require(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  lua_getglobal(L, "_require");
  lua_pushvalue(L, 1);
  if (modules_contains(&imported, name)) {
    lua_call(L, 1, 1);
  } else {
    modules_emplace(&imported, name);
    PROFILE(strdup(name), { lua_call(L, 1, 1); })
  }
  return 1;
}

static inline bool init_packages(lua_State *L) {
  // put builtin packages in preload
  lua_getglobal(L, "package");    // [package]
  lua_getfield(L, -1, "preload"); // [package, preload]
  for (usize i = 0; i < ARRAY_SIZE(builtin_modules); i++) {
    ModuleDef def = builtin_modules[i];
    lua_pushinteger(L, (long)i);                // [package, preload, i]
    lua_pushcclosure(L, l_module_preloader, 1); // [package, preload, cclosure]
    lua_setfield(L, -2, def.name);              // [package, preload]
  }

  lua_pop(L, 2); // []

  // override require for profiling
  lua_getglobal(L, "require");     // [require]
  lua_setglobal(L, "_require");    // []
  lua_pushcfunction(L, l_require); // [l_require]
  lua_setglobal(L, "require");     // []

  lua_getglobal(L, "require");
  lua_pushstring(L, "lfm._core");
  if (lfm_lua_pcall(L, 1, 0)) {
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }

  return true;
}

void lfm_lua_cb(lua_State *L, int ref, bool unref) {
  lfm_lua_push_callback(L, ref, unref); // [f]
  if (lfm_lua_pcall(L, 0, 0)) {         // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lfm_lua_cb1(lua_State *L, int ref, zsview line) {
  lfm_lua_push_callback(L, ref, false); // [f]
  lua_pushzsview(L, line);
  if (lfm_lua_pcall(L, 1, 0)) { // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lfm_lua_child_exit_cb(lua_State *L, int ref, int rstatus) {
  lfm_lua_push_callback(L, ref, true); // [f]
  lua_pushnumber(L, rstatus);          // [f, rstatus]
  if (lfm_lua_pcall(L, 1, 0)) {        // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

// line does not have to be nul terminated, in this case, len hase to be
// passed; len is negative, strlen(line) is used
void lfm_lua_child_stdout_cb(lua_State *L, int ref, const char *line,
                             isize len) {

  lfm_lua_push_callback(L, ref, line == NULL); // [f]
  if (!line) {
    lua_pop(L, 1); // []
    return;
  }

  if (len < 0)
    len = strlen(line);

  lua_pushlstring(L, line, len);                // [f, line]
  if (lfm_lua_pcall(L, 1, 00)) {                // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1)); // [err]
    lua_pop(L, 1);                              // []
  }
}

void lfm_lua_cb_with_count(lua_State *L, int ref, int count) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [f]
  if (count > 0)
    lua_pushnumber(L, count);                   // [f, count]
  if (lfm_lua_pcall(L, count > 0 ? 1 : 0, 0)) { // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lfm_lua_evaln(lua_State *L, const char *expr, int len) {
  log_debug("lua_eval %.*s", len, expr);
  lua_getglobal(L, "lfm");       // [lfm]
  lua_getfield(L, -1, "eval");   // [lfm, lfm.eval]
  lua_pushlstring(L, expr, len); // [lfm, lfm.eval, expr]
  if (lfm_lua_pcall(L, 1, 0)) {  // [lfm]
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

bool lfm_lua_filter(lua_State *L, int ref, zsview name) {
  lfm_lua_push_callback(L, ref, false);
  lua_pushzsview(L, name);
  if (lfm_lua_pcall(L, 1, 1)) { // []
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return lua_toboolean(L, -1);
}
