#pragma once

#include "../bytes.h"
#include "../stcutil.h"
#include "../vec_bytes.h"
#include "../vec_cstr.h"
#include "../vec_str.h"

#include <lauxlib.h>
#include <lua.h>

#define LUA_CHECK_ARGC(L, expected)                                            \
  do {                                                                         \
    if (lua_gettop(L) != (expected))                                           \
      return luaL_error(L, "Expected %d %s, got %d", (expected),               \
                        expected == 1 ? "argument" : "arguments",              \
                        lua_gettop(L));                                        \
  } while (0)

#define luaL_checkzsview(L, idx)                                               \
  (luaL_checktype((L), (idx), LUA_TSTRING), lua_tozsview((L), (idx)))

#define luaL_optzsview(L, idx, def)                                            \
  (lua_isnoneornil((L), (idx)) ? (def) : lua_tozsview((L), (idx)))

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

static inline void lua_pushbytes(lua_State *L, bytes bytes) {
  lua_pushlstring(L, bytes.data, bytes.size);
}

void lua_push_vec_cstr(lua_State *L, const vec_cstr *vec);

void lua_push_vec_bytes(lua_State *L, vec_bytes *vec);

void lua_read_vec_bytes(lua_State *L, int idx, vec_bytes *vec);

void lua_read_vec_str(lua_State *L, int idx, vec_str *vec);

void lua_read_vec_cstr(lua_State *L, int idx, vec_cstr *vec);

// Decode the passed chunk with string.buffer.decode and leave it
// on the stack
int lua_decode(lua_State *L, bytes chunk);

// Encode the object at index idx and return it in chunk
__lfm_nonnull()
int lua_encode(lua_State *L, int idx, bytes *chunk);

// convert the function at position idx into bytecode
int lua_string_dump(lua_State *L, int idx);

// removes ./?.lua and ./?.so from package.(c)path
// appends ~/.config/lfm/lua/... to package.path
void set_package_path(lua_State *L);
