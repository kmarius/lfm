#pragma once

#include "../containers.h"

#include <lua.h>

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
static inline struct bytes lua_to_bytes(lua_State *L, int idx) {
  size_t len;
  const char *s = lua_tolstring(L, idx, &len);
  char *data = memdup(s, len);
  if (data == NULL)
    len = 0;
  return (struct bytes){data, len};
}

static inline void lua_push_vec_bytes(lua_State *L, vec_bytes *vec) {
  lua_createtable(L, vec_bytes_size(vec), 0);
  size_t i = 1;
  c_foreach(it, vec_bytes, *vec) {
    lua_pushlstring(L, it.ref->data, it.ref->len);
    lua_rawseti(L, -2, i++);
  }
}

static inline void lua_read_vec_bytes(lua_State *L, int idx, vec_bytes *vec) {
  int n = lua_objlen(L, idx);
  vec_bytes_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_bytes_push_back(vec, lua_to_bytes(L, -1));
    lua_pop(L, 1);
  }
}

static inline void lua_read_vec_str(lua_State *L, int idx, vec_str *vec) {
  int n = lua_objlen(L, idx);
  vec_str_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_str_push_back(vec, lua_tostrdup(L, -1));
    lua_pop(L, 1);
  }
}

static inline void lua_read_vec_cstr(lua_State *L, int idx, vec_cstr *vec) {
  int n = lua_objlen(L, idx);
  vec_cstr_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_cstr_push_back(vec, lua_tocstr(L, -1));
    lua_pop(L, 1);
  }
}
