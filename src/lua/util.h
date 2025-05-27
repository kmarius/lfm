#pragma once

#include "../containers.h"
#include "../log.h"
#include "../stcutil.h"

#include <lauxlib.h>
#include <lua.h>

#define LUA_CHECK_ARGC(L, expected)                                            \
  do {                                                                         \
    if (lua_gettop(L) != (expected))                                           \
      return luaL_error(L, "Expected %d %s, got %d", (expected),               \
                        expected == 1 ? "argument" : "arguments",              \
                        lua_gettop(L));                                        \
  } while (0)

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
static inline bytes lua_tobytes(lua_State *L, int idx) {
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

static inline void lua_pushbytes(lua_State *L, bytes bytes) {
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

// encode lua value at at the given index with string.buffer.encode and leave it
// on the stack
static inline int lua_encode(lua_State *L, int idx) {
  lua_pushvalue(L, idx); // [value]

  lua_getglobal(L, "require");        // [value, require]
  lua_pushstring(L, "string.buffer"); // [value, require, "string.buffer"]

  int status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [value, err]
    lua_remove(L, -2);
    return status;
  }
  // [value, string.buffer]

  lua_getfield(L, -1, "encode"); // [value, string.buffer, encode]
  lua_remove(L, -2);             // [value, encode]
  lua_insert(L, -2);             // [encode, value]

  status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [err]
    return status;
  }
  // [bytes]

  return LUA_OK;
}

// decode string at at the given index with string.buffer.decode and leave it
// on the stack
static inline int lua_decode(lua_State *L, int idx) {
  lua_pushvalue(L, idx);              // [bytes]
  lua_getglobal(L, "require");        // [bytes, require]
  lua_pushstring(L, "string.buffer"); // [bytes, require, "string.buffer"]

  int status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [bytes, err]
    lua_remove(L, -2);
    return status;
  }
  // [bytes, string.buffer]

  lua_getfield(L, -1, "decode"); // [bytes, string.buffer, decode]
  lua_remove(L, -2);             // [bytes, decode]
  lua_insert(L, -2);             // [decode, bytes]

  status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [err]
    return status;
  }
  // [value]

  return LUA_OK;
}

// convert the function at position idx into bytecode
static inline int lua_string_dump(lua_State *L, int idx) {
  lua_getglobal(L, "string");  // [string]
  lua_getfield(L, -1, "dump"); // [string, string.dump]
  lua_remove(L, -2);           // [string.dump]
  lua_pushvalue(L, idx);       // [string.dump, func]
  int status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    return status;
  }
  // [bytecode]
  return LUA_OK;
}
