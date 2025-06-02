#include "util.h"

#include "../config.h"
#include "lua.h"

#include <linux/limits.h>

void set_package_path(lua_State *L) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");

  char buf[PATH_MAX];
  size_t len;

  const char *path = lua_tolstring(L, -1, &len);
  memcpy(buf, path, len + 1);

  // remove ./?.lua, beware of trailing nuls
  char *ptr = strstr(buf, "./?.lua");
  if (ptr != NULL) {
    int l = sizeof "./?.lua" - 1;
    if (buf[l] == ';') {
      l++;
    }
    memmove(ptr, ptr + l, len - (ptr - buf) + 1);
    len -= l;
  }

  // append ~/.config/lfm/lua/..
  snprintf(buf + len, sizeof buf - len, ";%s/lua/?.lua;%s/lua/?/init.lua",
           cstr_str(&cfg.configdir), cstr_str(&cfg.configdir));

  lua_pop(L, 1);
  lua_pushstring(L, buf);
  lua_setfield(L, -2, "path");

  lua_getfield(L, -1, "cpath");
  path = lua_tolstring(L, -1, &len);
  memcpy(buf, path, len + 1);

  // remove ./?.so
  ptr = strstr(buf, "./?.so");
  if (ptr != NULL) {
    int l = sizeof "./?.so" - 1;
    if (buf[l] == ';') {
      l++;
    }
    memmove(ptr, ptr + l, len - (ptr - buf) + 1);
    len -= l;
  }

  lua_pop(L, 1);
  lua_pushstring(L, buf);
  lua_setfield(L, -2, "cpath");

  lua_pop(L, 1);
}

// encode lua value at at the given index with string.buffer.encode and leave it
// on the stack
int lua_encode(lua_State *L, int idx, bytes *chunk) {
  // push this value now, otherwise a negative idx is wrong
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

  *chunk = lua_tobytes(L, -1);
  lua_pop(L, 1); // []

  return LUA_OK;
}

int lua_decode(lua_State *L, bytes chunk) {
  lua_getglobal(L, "require");        // [require]
  lua_pushstring(L, "string.buffer"); // [require, "string.buffer"]

  int status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [err]
    return status;
  }
  // [string.buffer]

  lua_getfield(L, -1, "decode"); // [string.buffer, decode]
  lua_remove(L, -2);             // [decode]
  lua_pushbytes(L, chunk);       // [decode, bytes]

  status = lua_pcall(L, 1, 1, 0);
  if (status != LUA_OK) {
    // [err]
    return status;
  }
  // [value]

  return LUA_OK;
}

void lua_read_vec_cstr(lua_State *L, int idx, vec_cstr *vec) {
  int n = lua_objlen(L, idx);
  vec_cstr_clear(vec);
  vec_cstr_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_cstr_push_back(vec, lua_tocstr(L, -1));
    lua_pop(L, 1);
  }
}

void lua_read_vec_str(lua_State *L, int idx, vec_str *vec) {
  int n = lua_objlen(L, idx);
  vec_str_clear(vec);
  vec_str_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_str_push_back(vec, lua_tostrdup(L, -1));
    lua_pop(L, 1);
  }
}

void lua_read_vec_bytes(lua_State *L, int idx, vec_bytes *vec) {
  int n = lua_objlen(L, idx);
  vec_bytes_clear(vec);
  vec_bytes_reserve(vec, n);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    vec_bytes_push_back(vec, lua_tobytes(L, -1));
    lua_pop(L, 1);
  }
}

void lua_push_vec_cstr(lua_State *L, const vec_cstr *vec) {
  lua_createtable(L, vec_cstr_size(vec), 0);
  int i = 1;
  c_foreach(it, vec_cstr, *vec) {
    lua_pushlstring(L, cstr_str(it.ref), cstr_size(it.ref));
    lua_rawseti(L, -2, i++);
  }
}

void lua_push_vec_bytes(lua_State *L, vec_bytes *vec) {
  lua_createtable(L, vec_bytes_size(vec), 0);
  size_t i = 1;
  c_foreach(it, vec_bytes, *vec) {
    lua_pushbytes(L, *it.ref);
    lua_rawseti(L, -2, i++);
  }
}

int lua_string_dump(lua_State *L, int idx) {
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
