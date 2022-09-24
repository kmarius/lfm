#pragma once

#include <lua.h>
#include <stdbool.h>

#define MIME_MAX 128  // arbitrary

int luaopen_rifle(lua_State *L);

bool get_mimetype(const char *path, char *dest);
