#include "util.h"

#include "../config.h"

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
