#pragma once

#include <lua.h>

int luaopen_api(lua_State *L);
int luaopen_options(lua_State *L);
int luaopen_fn(lua_State *L);
int luaopen_lfm(lua_State *L);
int luaopen_log(lua_State *L);
int luaopen_paths(lua_State *L);
int luaopen_rifle(lua_State *L);
