#pragma once

#include <lua.h>
#include <stdbool.h>

// arbitrary
#define MIME_MAX 128

int lua_register_rifle(lua_State *L);

int lua_rifle_clear(lua_State *L);

bool get_mimetype(const char *path, char *dest);
