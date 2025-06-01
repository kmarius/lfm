#include "private.h"

#include "../bytes.h"
#include "../config.h"
#include "../lfm.h"
#include "../lua/thread.h"
#include "../lua/util.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

static int init_lua_thread_state() {
  if (L_thread == NULL) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    set_package_path(L);

    L_thread = L;
  }
  return 0;
}

struct lua_data {
  struct result super;
  Async *async;
  bytes chunk;  // lua code to execute
  bytes arg;    // optional argument
  bytes result; // error string if error == true, serialized result, otherwise
  bool error;   // either loadstring or the code itself returned an error
  int ref;      // ref to the callback
};

static void lua_result_destroy(void *p) {
  // TODO: check if we unref in all cases
  struct lua_data *res = p;
  bytes_drop(&res->chunk);
  bytes_drop(&res->arg);
  bytes_drop(&res->result);
  xfree(p);
}

static void lua_result_callback(void *p, Lfm *lfm) {
  struct lua_data *res = p;
  lua_State *L = lfm->L;
  if (L == NULL || res->ref == 0) {
    // lfm is shutting down; or no callback
    goto cleanup;
  }

  lua_get_callback(L, res->ref, true); // [cb]

  if (res->error) {
    // res->result is error message, nil inserted later
    lua_pushbytes(L, res->result); // [cb, err]
    goto err;
  }

  if (bytes_is_empty(res->result)) {
    // result was nil
    lua_pushnil(L); // [cb, nil]
  } else {
    lua_pushbytes(L, res->result); // [cb, bytes]
    if (lua_decode(L, -1)) {
      // [cb, bytes, err]
      lua_remove(L, -2);
      goto err;
    }
    // [cb, bytes, res]
    lua_remove(L, -2); // [cb, res]
  }

  // [cb, res]

  // everything went well

  if (llua_pcall(L, 1, 0)) {
    // [err]
    ui_error(&lfm->ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  // []

cleanup:
  lua_result_destroy(p);
  return;

err:
  // [cb, err]
  lua_pushnil(L);    // [cb, err, nil]
  lua_insert(L, -2); // [cb, nil, err]
  if (llua_pcall(L, 2, 0)) {
    // [err]
    ui_error(&lfm->ui, "%s", lua_tostring(L, -1));
    lua_pop(L, 1); // []
  }
  goto cleanup;
}

void async_lua_worker(void *arg) {
  struct lua_data *work = arg;

  if (L_thread == NULL) {
    if (init_lua_thread_state()) {
      // [err]
      work->result = lua_tobytes(L_thread, -1);
      lua_pop(L_thread, 1); // []
      goto err;
    }
  }

  lua_State *L = L_thread; // []

  bytes chunk = work->chunk;
  if (luaL_loadbuffer(L, chunk.data, chunk.len, "chunk")) {
    // [err]
    work->result = lua_tobytes(L, -1);
    lua_pop(L, 1);
    goto err;
  }

  // [func]

  int nargs = 0;
  if (!bytes_is_empty(work->arg)) {
    lua_pushbytes(L, work->arg); // [func, bytes]
    if (lua_decode(L, -1)) {
      // [func, bytes, err]
      work->result = lua_tobytes(L, -1);
      lua_pop(L, 3);
      goto err;
    }
    // [func, bytes, arg]

    lua_remove(L, -2); // [func, arg]
    nargs++;
  }

  // [func, arg]

  if (lua_pcall(L, nargs, 1, 0)) {
    // [err]
    work->result = lua_tobytes(L, -1);
    lua_pop(L, 1);
    goto err;
  }

  // [res]

  if (work->ref == 0) {
    // don't serialize if there is no callback
    // [nil]
    work->result = bytes_init();
    lua_pop(L, 1);
  } else {
    // [res]
    if (lua_encode(L, -1)) {
      // [res, err]
      work->result = lua_tobytes(L, -1);
      lua_pop(L, 2); // []
      goto err;
    }
    // [res, bytes]

    work->result = lua_tobytes(L, -1);
    lua_pop(L, 2); // []
  }

end:
  enqueue_and_signal(work->async, (struct result *)work);
  if (L_thread != NULL) {
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")
  }
  return;

err:
  work->error = true;
  goto end;
}

void async_lua(Async *async, bytes *chunk, bytes *arg, int ref) {
  struct lua_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &lua_result_callback;
  work->super.destroy = &lua_result_destroy;

  work->async = async;
  work->chunk = bytes_move(chunk);
  work->arg = bytes_move(arg);
  work->ref = ref;

  log_trace("async_lua");
  tpool_add_work(async->tpool, async_lua_worker, work, true);
}

struct lua_preview_data {
  struct result super;
  Async *async;
  Preview *preview; // not guaranteed to exist, do not touch
  bytes chunk;      // lua code to execute
  char *path;       // optional argument
  int width;
  int height;
  Preview *update;
  struct validity_check64 check;
};

static void lua_preview_destroy(void *p) {
  struct lua_preview_data *res = p;
  bytes_drop(&res->chunk);
  preview_destroy(res->update);
  free(res->path);
  free(res);
}

static void lua_preview_callback(void *p, Lfm *lfm) {
  struct lua_preview_data *res = p;
  if (CHECK_PASSES(res->check)) {
    preview_update(res->preview, res->update);
    ui_redraw(&lfm->ui, REDRAW_PREVIEW);
    res->update = NULL;
  }
  lua_preview_destroy(p);
}

void async_lua_preview_worker(void *arg) {
  struct lua_preview_data *work = arg;

  Preview *pv = preview_create_and_stat(zsview_from(work->path), work->height,
                                        work->width);
  work->update = pv;

  if (L_thread == NULL) {
    if (init_lua_thread_state()) {
      // [err]
      preview_error(pv, "%s", lua_tostring(L_thread, -1));
      lua_pop(L_thread, 1); // []
      goto err;
    }
  }

  // [encode, decode]
  lua_State *L = L_thread;

  bytes chunk = work->chunk;
  if (luaL_loadbuffer(L, chunk.data, chunk.len, chunk.data)) {
    // [encode, decode, err]
    preview_error(pv, "%s", lua_tostring(L_thread, -1));
    lua_pop(L, 1);
    goto err;
  }

  // [encode, decode, func]

  lua_pushstring(L, work->path);   // [encode, decode, func, path]
  lua_pushnumber(L, work->height); // [encode, decode, func, path, height]
  lua_pushnumber(L, work->width); // [encode, decode, func, path, height, width]
  int nargs = 3;

  if (lua_pcall(L, nargs, 1, 0)) {
    // [encode, decode, err]
    preview_error(pv, "%s", lua_tostring(L_thread, -1));
    lua_pop(L, 1);
    goto err;
  }

  // [encode, decode, res]

  if (lua_isnil(L, -1)) {
    // can not serialize nil, and we don't serialize if there is no callback
    // [encode, decode, nil]
    // TODO: leave preview empty? dont return?
    lua_pop(L, 1);
  } else {
    // [encode, decode, res]
    int n = lua_objlen(L, -1);
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, -1, i);
      vec_cstr_push_back(&pv->lines, lua_tocstr(L, -1));
      lua_pop(L, 1);
    }
    lua_pop(L, 1); // [encode, decode]
  }

end:
  enqueue_and_signal(work->async, (struct result *)work);
  if (L_thread != NULL) {
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")
  }
  return;

err:
  // nothing, currently
  goto end;
}

void async_lua_preview(Async *async, struct Preview *pv) {
  struct lua_preview_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &lua_preview_callback;
  work->super.destroy = &lua_preview_destroy;

  pv->status =
      pv->status == PV_LOADING_DELAYED ? PV_LOADING_INITIAL : PV_LOADING_NORMAL;
  pv->loading = true;

  work->async = async;
  work->chunk = bytes_clone(cfg.lua_previewer);
  work->preview = pv;
  work->path = cstr_strdup(preview_path(pv));
  work->width = to_lfm(async)->ui.preview.x;
  work->height = to_lfm(async)->ui.preview.y;
  CHECK_INIT(work->check, to_lfm(async)->loader.preview_cache_version);

  log_trace("async_lua_preview %s", work->path);
  tpool_add_work(async->tpool, async_lua_preview_worker, work, true);
}
