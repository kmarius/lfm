#pragma once

#include "defs.h"
#include "lua/lfmlua.h"
#include "lua/util.h" // lfm <-> stc functions

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
i32 lfm_add_hook(struct Lfm *lfm, lfm_hook_id hook, i32 ref);

// Returns the reference of the callback, or 0 if no hook was removed.
i32 lfm_remove_hook(struct Lfm *lfm, i32 id);

// apply all changes made to hooks during a hook callback
void lfm_apply_hook_changes(struct Lfm *lfm);

// returns -1 for invalid hook name
static inline lfm_hook_id hook_name_to_id(const char *name) {
  for (i32 i = 0; i < LFM_NUM_HOOKS; i++) {
    if (strcmp(name, hook_str[i]) == 0) {
      return i;
    }
  }
  return -1;
}

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
