#include "private.h"

#include "lfm.h"
#include "log.h"
#include "lua/thread.h"
#include "lua/util.h"
#include "types/bytes.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

struct lua_work {
  struct result super;
  struct async_ctx *async;
  bytes chunk; // lua code to execute
  vec_bytes args;
  bytes result; // error string if error == true, serialized result, otherwise
  bool error;   // either loadstring or the code itself returned an error
  int ref;      // ref to the callback
};

static void destroy(void *p) {
  // TODO: check if we unref in all cases
  struct lua_work *work = p;
  bytes_drop(&work->chunk);
  vec_bytes_drop(&work->args);
  bytes_drop(&work->result);
  xfree(p);
}

static void callback(void *p, Lfm *lfm) {
  struct lua_work *work = p;
  lua_State *L = lfm->L;
  if (unlikely(L == NULL || work->ref == 0)) {
    // lfm is shutting down; or no callback
    goto cleanup;
  }

  lfm_lua_push_callback(L, work->ref, true); // [cb]

  if (unlikely(work->error)) {
    // res->result is error message, nil inserted later
    lua_pushbytes(L, work->result); // [cb, err]
    goto err;
  }

  if (bytes_is_empty(work->result)) {
    // result was nil
    lua_pushnil(L); // [cb, nil]
  } else {
    if (unlikely(lua_decode(L, work->result))) {
      // [cb, err]
      goto err;
    }
  }

  // [cb, res]

  // everything went well

  if (unlikely(lfm_lua_pcall(L, 1, 0))) {
    // [err]
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  // []

cleanup:
  return;

err:
  // [cb, err]
  lua_pushnil(L);    // [cb, err, nil]
  lua_insert(L, -2); // [cb, nil, err]
  if (lfm_lua_pcall(L, 2, 0)) {
    // [err]
    lfm_errorf(lfm, "%s", lua_tostring(L, -1));
    lua_pop(L, 1); // []
  }
  goto cleanup;
}

void worker(void *arg) {
  struct lua_work *work = arg;

  if (unlikely(L_thread == NULL)) {
    if (L_thread_init()) {
      // [err]
      work->result = lua_tobytes(L_thread, -1);
      lua_pop(L_thread, 1); // []
      goto err;
    }
  }

  lua_State *L = L_thread; // []

  bytes chunk = work->chunk;
  if (unlikely(luaL_loadbuffer(L, chunk.buf, chunk.size, "chunk"))) {
    // [err]
    work->result = lua_tobytes(L, -1);
    lua_pop(L, 1);
    goto err;
  }

  // [func]

  int nargs = 0;
  if (!vec_bytes_is_empty(&work->args)) {
    c_foreach(it, vec_bytes, work->args) {
      if (unlikely(lua_decode(L, *it.ref))) {
        // unlikely that we can't docode something we encoded earlier
        // [func, args..., err]
        for (i32 i = 0; i < nargs; i++)
          lua_remove(L, -2);
        // [func, err]
        work->result = lua_tobytes(L, -1);
        lua_pop(L, 2);
        goto err;
      }
      nargs++;
    }
    // [func, args...]
  }

  // [func, args...]

  if (unlikely(lua_pcall(L, nargs, 1, 0))) {
    // [err]
    work->result = lua_tobytes(L, -1);
    lua_pop(L, 1);
    goto err;
  }

  // [res]

  // don't serialize if there is no callback
  if (work->ref != 0) {
    if (unlikely(lua_encode(L, -1, &work->result))) {
      // [res, err]
      work->result = lua_tobytes(L, -1);
      lua_pop(L, 2); // []
      goto err;
    }
  } else {
    work->result = bytes_init();
  }
  lua_pop(L, 1); // []

end:
  submit_async_result(work->async, (struct result *)work);
  if (likely(L_thread != NULL))
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")

  return;

err:
  work->error = true;
  goto end;
}

void async_lua(struct async_ctx *async, bytes chunk, vec_bytes args, int ref) {
  struct lua_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

  work->async = async;
  work->chunk = chunk;
  work->args = args;
  work->ref = ref;

  log_trace("async_lua");
  tpool_add_work(async->tpool, worker, work, true);
}
