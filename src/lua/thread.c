#include "thread.h"

_Thread_local lua_State *L_thread = NULL;

void L_thread_destroy() {
  if (L_thread) {
    lua_close(L_thread);
    L_thread = NULL;
  }
}
