#pragma once

// This header contains Lua-dependent hook macros.
// For the hook enum and function declarations without Lua dependencies,
// include hooks_types.h instead.

#include "hooks_types.h"
#include "lua/lfmlua.h"
#include "lua/util.h" // lfm <-> stc functions

#include <lauxlib.h>

// generic push macro (add missing types as needed)
#define LUA_PUSH_GENERIC(L, ARG)                                               \
  _Generic((ARG),                                                              \
      const char *: lua_pushstring,                                            \
      char *: lua_pushstring,                                                  \
      const cstr *: lua_pushcstr,                                              \
      cstr *: lua_pushcstr,                                                    \
      zsview: lua_pushzsview,                                                  \
      float: lua_pushnumber,                                                   \
      i32: lua_pushnumber)((L), (ARG))

#define LFM_RUN_HOOK_(lfm, hook, N, ...)                                       \
  do {                                                                         \
    if (!vec_int_is_empty(&(lfm)->hook_refs[hook])) {                          \
      (lfm)->hook_callback_depth++;                                            \
      FOR_EACH1(LUA_PUSH_GENERIC, (lfm)->L, __VA_ARGS__);                      \
      c_foreach(it, vec_int, (lfm)->hook_refs[hook]) {                         \
        lfm_lua_push_callback((lfm)->L, *it.ref, false);                       \
        if (N) /* the weird comparison shuts up the compiler */                \
          for (u32 hooks_h_i = 0; hooks_h_i < (N ? N : 1); hooks_h_i++)        \
            lua_pushvalue((lfm)->L, -(N + 1));                                 \
        if (unlikely(lfm_lua_pcall((lfm)->L, N, 0) != LUA_OK)) {               \
          lfm_errorf(lfm, "%s", lua_tostring((lfm)->L, -1));                   \
          lua_pop((lfm)->L, 1);                                                \
        }                                                                      \
      }                                                                        \
      if (N)                                                                   \
        lua_pop((lfm)->L, N);                                                  \
      if (--(lfm)->hook_callback_depth == 0)                                   \
        lfm_apply_hook_changes(lfm);                                           \
    }                                                                          \
  } while (0)

#define LFM_RUN_HOOK(lfm, hook, ...)                                           \
  LFM_RUN_HOOK_((lfm), (hook), (FOR_EACH_NARG(__VA_ARGS__)), __VA_ARGS__)
