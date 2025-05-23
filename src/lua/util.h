#pragma once

#include "../containers.h"
#include "../stcutil.h"
#include "lauxlib.h"

#include <lua.h>

// common idioms mostly for conversion from lua types to stc types

static inline void lua_pushcstr(lua_State *L, const cstr *cstr) {
  lua_pushlstring(L, cstr_str(cstr), cstr_size(cstr));
}

static inline void lua_pushzsview(lua_State *L, zsview zv) {
  lua_pushlstring(L, zv.str, zv.size);
}

static inline zsview lua_tozsview(lua_State *L, int idx) {
  size_t len;
  const char *str = lua_tolstring(L, idx, &len);
  return zsview_from_n(str ? str : "", len);
}

#define luaL_checkzsview(L, idx)                                               \
  (luaL_checktype((L), (idx), LUA_TSTRING), lua_tozsview((L), (idx)))

#define luaL_optzsview(L, idx, def)                                            \
  (lua_isnoneornil((L), (idx)) ? (def) : lua_tozsview((L), (idx)))

// efficiently create a copy of the string repr of the value at position idx
static inline char *lua_tostrdup(lua_State *L, int idx) {
  size_t len;
  const char *s = lua_tolstring(L, idx, &len);
  return strndup(s, len);
}

static inline cstr lua_tocstr(lua_State *L, int idx) {
  size_t len;
  const char *str = lua_tolstring(L, idx, &len);
  return cstr_with_n(str, len);
}

// convert value at position idx to a string and then into a struct bytes
static inline struct bytes lua_tobytes(lua_State *L, int idx) {
  size_t len;
  const char *data = lua_tolstring(L, idx, &len);
  return bytes_from_n(data, len);
}

static inline void lua_push_vec_cstr(lua_State *L, const vec_cstr *vec) {
  lua_createtable(L, vec_cstr_size(vec), 0);
  int i = 1;
  c_foreach(it, vec_cstr, *vec) {
    lua_pushlstring(L, cstr_str(it.ref), cstr_size(it.ref));
    lua_rawseti(L, -2, i++);
  }
}

static inline void lua_pushbytes(lua_State *L, struct bytes bytes) {
  lua_pushlstring(L, bytes.data, bytes.len);
}

static inline void lua_push_vec_bytes(lua_State *L, vec_bytes *vec) {
  lua_createtable(L, vec_bytes_size(vec), 0);
  size_t i = 1;
  c_foreach(it, vec_bytes, *vec) {
    lua_pushbytes(L, *it.ref);
    lua_rawseti(L, -2, i++);
  }
}

static inline void lua_read_vec_bytes(lua_State *L, int idx, vec_bytes *vec) {
  int n = lua_objlen(L, idx);
  vec_bytes_clear(vec);
  vec_bytes_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_bytes_push_back(vec, lua_tobytes(L, -1));
    lua_pop(L, 1);
  }
}

static inline void lua_read_vec_str(lua_State *L, int idx, vec_str *vec) {
  int n = lua_objlen(L, idx);
  vec_str_clear(vec);
  vec_str_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_str_push_back(vec, lua_tostrdup(L, -1));
    lua_pop(L, 1);
  }
}

static inline void lua_read_vec_cstr(lua_State *L, int idx, vec_cstr *vec) {
  int n = lua_objlen(L, idx);
  vec_cstr_clear(vec);
  vec_cstr_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_cstr_push_back(vec, lua_tocstr(L, -1));
    lua_pop(L, 1);
  }
}
