#include "thread.h"
#include "lfmlua.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

_Thread_local lua_State *L_thread = NULL;

int L_thread_init() {
  if (unlikely(L_thread == NULL)) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    set_package_path(L);
    lfm_lua_init_thread(L);

    L_thread = L;
  }
  return 0;
}

void L_thread_destroy() {
  if (L_thread) {
    lua_close(L_thread);
    L_thread = NULL;
  }
}
