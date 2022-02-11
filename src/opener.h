#pragma once

#include <lua.h>

int lua_register_opener(lua_State *L);

int lua_opener_clear(lua_State *L);
