#include "../path.h"
#include "../tokenize.h"
#include "lua.h"
#include "private.h"

#include <lauxlib.h>

#include <linux/limits.h>
#include <unistd.h>

static int l_fn_normalize(lua_State *L) {
  char buf[PATH_MAX + 1];
  zsview path = luaL_checkzsview(L, 1);
  zsview normalized = path_normalize3(path, fm_getpwd_str(fm), buf, sizeof buf);
  if (zsview_is_empty(normalized)) {
    return luaL_error(L, "path too long");
  }
  lua_pushzsview(L, normalized);
  return 1;
}

static int l_fn_mime(lua_State *L) {
  char mime[256];
  const char *path = luaL_checkstring(L, 1);
  if (get_mimetype(path, mime, sizeof mime)) {
    lua_pushstring(L, mime);
    return 1;
  }
  return 0;
}

static int l_fn_tokenize(lua_State *L) {
  size_t len;
  const char *string = luaL_checklstring(L, 1, &len);
  char *buf = xmalloc(len + 1);
  const char *pos1, *tok;
  char *pos2;
  if ((tok = tokenize(string, buf, &pos1, &pos2))) {
    lua_pushstring(L, tok);
  }
  lua_newtable(L);
  int i = 1;
  while ((tok = tokenize(NULL, NULL, &pos1, &pos2))) {
    lua_pushstring(L, tok);
    lua_rawseti(L, -2, i++);
  }
  xfree(buf);
  return 2;
}

static int l_fn_split_last(lua_State *L) {
  size_t len;
  const char *string = luaL_checklstring(L, 1, &len);
  if (len == 0) {
    return luaL_error(L, "empty string");
  }
  bool esc = false;
  unsigned last = 0;
  for (unsigned i = 0; i < len; i++) {
    if (string[i] == '\\') {
      esc = !esc;
    } else {
      if (string[i] == ' ' && !esc) {
        last = i + 1;
      }
      esc = false;
    }
  }
  lua_pushlstring(L, string, last);
  lua_pushlstring(L, string + last, len - last);
  return 2;
}

static int l_fn_unquote_space(lua_State *L) {
  size_t len;
  const char *string = luaL_checklstring(L, 1, &len);
  char *buf = xmalloc(len + 1);
  char *t = buf;
  for (const char *s = string; *s != 0; s++) {
    if (*s != '\\' || *(s + 1) != ' ') {
      *t++ = *s;
    }
  }
  lua_pushlstring(L, buf, t - buf);
  xfree(buf);
  return 1;
}

static int l_fn_quote_space(lua_State *L) {
  size_t len;
  const char *string = luaL_checklstring(L, 1, &len);
  char *buf = xmalloc(len * 2 + 1);
  char *t = buf;
  for (const char *s = string; *s; s++) {
    if (*s == ' ') {
      *t++ = '\\';
    }
    *t++ = *s;
  }
  lua_pushlstring(L, buf, t - buf);
  xfree(buf);
  return 1;
}

static int l_fn_getpid(lua_State *L) {
  lua_pushinteger(L, getpid());
  return 1;
}

static int l_fn_getcwd(lua_State *L) {
  const char buf[PATH_MAX + 1];
  const char *cwd = getcwd((char *)buf, sizeof buf - 1);
  lua_pushstring(L, cwd ? cwd : "");
  return 1;
}

static int l_fn_getpwd(lua_State *L) {
  lua_pushcstr(L, fm_getpwd(fm));
  return 1;
}

static const struct luaL_Reg fn_lib[] = {{"split_last", l_fn_split_last},
                                         {"quote_space", l_fn_quote_space},
                                         {"unquote_space", l_fn_unquote_space},
                                         {"tokenize", l_fn_tokenize},
                                         {"mime", l_fn_mime},
                                         {"normalize", l_fn_normalize},
                                         {"getpid", l_fn_getpid},
                                         {"getcwd", l_fn_getcwd},
                                         {"getpwd", l_fn_getpwd},
                                         {NULL, NULL}};

int luaopen_fn(lua_State *L) {
  lua_newtable(L);
  luaL_register(L, NULL, fn_lib);
  return 1;
}
