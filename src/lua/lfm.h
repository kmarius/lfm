#pragma once

#include <lua.h>

int luaopen_cmd(lua_State *L);
int luaopen_config(lua_State *L);
int luaopen_fm(lua_State *L);
int luaopen_fn(lua_State *L);
int luaopen_lfm(lua_State *L);
int luaopen_log(lua_State *L);
int luaopen_rifle(lua_State *L);
int luaopen_ui(lua_State *L);
