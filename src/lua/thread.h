#pragma once

#include <lua.h>

extern _Thread_local lua_State *L_thread;

// call this before thread exit
void L_thread_destroy();
