#pragma once

#include "log.h"
#include "lua/lfmlua.h"
#include "lua/util.h"

#include <assert.h>
#include <lauxlib.h>

struct Lfm;

typedef enum {
  LFM_HOOK_RESIZED = 0,
  LFM_HOOK_ENTER,
  LFM_HOOK_EXITPRE,
  LFM_HOOK_CHDIRPRE,
  LFM_HOOK_CHDIRPOST,
  LFM_HOOK_PASTEBUF,
  LFM_HOOK_SELECTION,
  LFM_HOOK_DIRLOADED,
  LFM_HOOK_DIRUPDATED,
  LFM_HOOK_MODECHANGED,
  LFM_HOOK_FOCUSGAINED,
  LFM_HOOK_FOCUSLOST,
  LFM_HOOK_EXECPRE,
  LFM_HOOK_EXECPOST,
  LFM_NUM_HOOKS
} lfm_hook_id;

extern const char *hook_str[LFM_NUM_HOOKS];

void lfm_hooks_init(struct Lfm *lfm);

void lfm_hooks_deinit(struct Lfm *lfm);

// Returns an id with which it can be removed later
int lfm_add_hook(struct Lfm *lfm, lfm_hook_id hook, int ref);

// Returns the reference of the callback, or 0 if no hook was removed.
int lfm_remove_hook(struct Lfm *lfm, int id);

// make variadic lfm_run_hook macro that delegates to the macro with the
// correct number of arguments (currently, 0 to 3 arguments)
#define EXPAND(x) x
#define GET_MACRO(_1, _2, _3, _4, _5, name, ...) name
#define lfm_run_hook(...)                                                      \
  EXPAND(GET_MACRO(__VA_ARGS__, _lfm_run_hook3, _lfm_run_hook2,                \
                   _lfm_run_hook1, _lfm_run_hook0)(__VA_ARGS__))

// generic push macro (add missing types as needed)
#define lua_push(L, ARG)                                                       \
  _Generic((ARG),                                                              \
      const char *: lua_pushstring,                                            \
      char *: lua_pushstring,                                                  \
      cstr *: lua_pushcstr,                                                    \
      const cstr *: lua_pushcstr,                                              \
      float: lua_pushnumber,                                                   \
      int: lua_pushnumber)((L), (ARG))

// Gets the previously stored (via lua_set_callback) element with reference ref
// from the registry and leaves it at the top of the stack.
static inline void lua_get_callback(lua_State *L, int ref, bool unref) {
  assert(ref > 0);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref); // [elem]
  if (unref) {
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
  }
  assert(!lua_isnoneornil(L, -1));
}

#define _lfm_run_hook0(lfm, hook)                                              \
  do {                                                                         \
    log_trace("running hook: %s", hook_str[(hook)]);                           \
    c_foreach(it, vec_int, (lfm)->hook_refs[(hook)]) {                         \
      lua_get_callback((lfm)->L, *it.ref, false);                              \
      if (llua_pcall((lfm)->L, 0, 0) != LUA_OK) {                              \
        ui_error(&(lfm)->ui, "%s", lua_tostring((lfm)->L, -1));                \
        lua_pop((lfm)->L, 1);                                                  \
      }                                                                        \
    }                                                                          \
  } while (false)

#define _lfm_run_hook1(lfm, hook, ARG1)                                        \
  do {                                                                         \
    log_trace("running hook: %s", hook_str[(hook)]);                           \
    lua_push((lfm)->L, (ARG1)); /* push ARG1 */                                \
    c_foreach(it, vec_int, (lfm)->hook_refs[(hook)]) {                         \
      lua_get_callback((lfm)->L, *it.ref, false);                              \
      lua_pushvalue((lfm)->L, -2); /* push copy of ARG1 */                     \
      if (llua_pcall((lfm)->L, 1, 0) != LUA_OK) {                              \
        ui_error(&(lfm)->ui, "%s", lua_tostring((lfm)->L, -1));                \
        lua_pop((lfm)->L, 1);                                                  \
      }                                                                        \
    }                                                                          \
    lua_pop((lfm)->L, 1); /* pop ARG1 */                                       \
  } while (false)

#define _lfm_run_hook2(lfm, hook, ARG1, ARG2)                                  \
  do {                                                                         \
    log_trace("running hook: %s", hook_str[(hook)]);                           \
    lua_push((lfm)->L, (ARG1)); /* push ARG1 */                                \
    lua_push((lfm)->L, (ARG2)); /* push ARG2 */                                \
    c_foreach(it, vec_int, (lfm)->hook_refs[(hook)]) {                         \
      lua_get_callback((lfm)->L, *it.ref, false);                              \
      lua_pushvalue((lfm)->L, -3); /* push copy of ARG1 */                     \
      lua_pushvalue((lfm)->L, -3); /* push copy of ARG2 (stack changed) */     \
      if (llua_pcall((lfm)->L, 2, 0) != LUA_OK) {                              \
        ui_error(&(lfm)->ui, "%s", lua_tostring((lfm)->L, -1));                \
        lua_pop((lfm)->L, 1);                                                  \
      }                                                                        \
    }                                                                          \
    lua_pop((lfm)->L, 2); /* pop ARG1 */                                       \
  } while (false)

#define _lfm_run_hook3(lfm, hook, ARG1, ARG2, ARG3)                            \
  do {                                                                         \
    log_trace("running hook: %s", hook_str[(hook)]);                           \
    lua_push((lfm)->L, (ARG1)); /* push ARG1 */                                \
    lua_push((lfm)->L, (ARG2)); /* push ARG2 */                                \
    lua_push((lfm)->L, (ARG3)); /* push ARG2 */                                \
    c_foreach(it, vec_int, (lfm)->hook_refs[(hook)]) {                         \
      lua_get_callback((lfm)->L, *it.ref, false);                              \
      lua_pushvalue((lfm)->L, -4); /* push copy of ARG1 */                     \
      lua_pushvalue((lfm)->L, -4); /* push copy of ARG2 (stack changed) */     \
      lua_pushvalue((lfm)->L, -4); /* push copy of ARG3 (stack changed) */     \
      if (llua_pcall((lfm)->L, 3, 0) != LUA_OK) {                              \
        ui_error(&(lfm)->ui, "%s", lua_tostring((lfm)->L, -1));                \
        lua_pop((lfm)->L, 1);                                                  \
      }                                                                        \
    }                                                                          \
    lua_pop((lfm)->L, 3); /* pop ARG1 */                                       \
  } while (false)
