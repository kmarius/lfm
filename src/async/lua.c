#include "private.h"

#include "config.h"
#include "lfm.h"
#include "log.h"
#include "lua/thread.h"
#include "lua/util.h"
#include "types/bytes.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

static int init_lua_thread_state() {
  if (L_thread == NULL) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    set_package_path(L);
    lfm_lua_init_thread(L);

    L_thread = L;
  }
  return 0;
}

struct lua_data {
  struct result super;
  struct async_ctx *async;
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
  if (unlikely(L == NULL || res->ref == 0)) {
    // lfm is shutting down; or no callback
    goto cleanup;
  }

  lfm_lua_push_callback(L, res->ref, true); // [cb]

  if (unlikely(res->error)) {
    // res->result is error message, nil inserted later
    lua_pushbytes(L, res->result); // [cb, err]
    goto err;
  }

  if (bytes_is_empty(res->result)) {
    // result was nil
    lua_pushnil(L); // [cb, nil]
  } else {
    if (unlikely(lua_decode(L, res->result))) {
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

void async_lua_worker(void *arg) {
  struct lua_data *work = arg;

  if (unlikely(L_thread == NULL)) {
    if (init_lua_thread_state()) {
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
  if (!bytes_is_empty(work->arg)) {
    if (lua_decode(L, work->arg)) {
      // [func, err]
      work->result = lua_tobytes(L, -1);
      lua_pop(L, 2);
      goto err;
    }
    // [func, arg]
    nargs++;
  }

  // [func, arg]

  if (unlikely(lua_pcall(L, nargs, 1, 0))) {
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
    if (unlikely(lua_encode(L, -1, &work->result))) {
      // [res, err]
      work->result = lua_tobytes(L, -1);
      lua_pop(L, 2); // []
      goto err;
    }
    // [res]
    lua_pop(L, 1); // []
  }

end:
  enqueue_and_signal(work->async, (struct result *)work);
  if (likely(L_thread != NULL))
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")

  return;

err:
  work->error = true;
  goto end;
}

void async_lua(struct async_ctx *async, bytes chunk, bytes arg, int ref) {
  struct lua_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &lua_result_callback;
  work->super.destroy = &lua_result_destroy;

  work->async = async;
  work->chunk = chunk;
  work->arg = arg;
  work->ref = ref;

  log_trace("async_lua");
  tpool_add_work(async->tpool, async_lua_worker, work, true);
}

struct lua_preview_data {
  struct result super;
  struct async_ctx *async;
  Preview *preview;
  bytes chunk; // lua code to execute
  int width;
  int height;
  Preview *update;
};

static void lua_preview_destroy(void *p) {
  struct lua_preview_data *res = p;
  bytes_drop(&res->chunk);
  preview_destroy(res->update);
  preview_dec_ref(res->preview);
  free(res);
}

static void lua_preview_callback(void *p, Lfm *lfm) {
  struct lua_preview_data *res = p;
  preview_update(res->preview, res->update);
  res->update = NULL;
  ui_redraw(&lfm->ui, REDRAW_PREVIEW);
  set_result_erase(&lfm->async.in_progress.lua_previews, p);
}

void async_lua_preview_worker(void *arg) {
  struct lua_preview_data *work = arg;

  Preview *pv = preview_create_and_stat(preview_path(work->preview),
                                        work->height, work->width);
  work->update = pv;

  if (unlikely(L_thread == NULL)) {
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
  if (unlikely(luaL_loadbuffer(L, chunk.buf, chunk.size, "chunk"))) {
    // [encode, decode, err]
    preview_error(pv, "%s", lua_tostring(L_thread, -1));
    lua_pop(L, 1);
    goto err;
  }

  // [encode, decode, func]

  lua_pushzsview(L,
                 preview_path(work->preview)); // [encode, decode, func, path]
  lua_pushnumber(L, work->height); // [encode, decode, func, path, height]
  lua_pushnumber(L, work->width); // [encode, decode, func, path, height, width]
  int nargs = 3;

  if (unlikely(lua_pcall(L, nargs, 1, 0))) {
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
    if (lua_type(L, -1) != LUA_TTABLE) {
      lua_newtable(L);
      lua_insert(L, -2);
      lua_tostring(L, -1);
      lua_rawseti(L, -2, 1);
    }

    // [encode, decode, res]
    int n = lua_objlen(L, -1);
    cstr buf = cstr_init();
    for (int i = 1; i <= n; i++) {
      lua_rawgeti(L, -1, i);
      cstr_append_zv(&buf, lua_tozsview(L, -1));
      cstr_append(&buf, "\n");
      lua_pop(L, 1);
    }
    bytes_take_cstr(&pv->data, buf);
    lua_pop(L, 1); // [encode, decode]
  }

end:
  enqueue_and_signal(work->async, (struct result *)work);
  if (likely(L_thread != NULL))
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")

  return;

err:
  // nothing, currently
  goto end;
}

void async_lua_preview(struct async_ctx *async, struct Preview *pv) {
  struct lua_preview_data *work = xcalloc(1, sizeof *work);
  work->super.callback = &lua_preview_callback;
  work->super.destroy = &lua_preview_destroy;

  pv->status =
      pv->status == PV_LOADING_DELAYED ? PV_LOADING_INITIAL : PV_LOADING_NORMAL;
  pv->loading = true;

  work->async = async;
  work->chunk = bytes_clone(cfg.lua_previewer);
  work->preview = preview_inc_ref(pv);
  work->width = to_lfm(async)->ui.preview.x;
  work->height = to_lfm(async)->ui.preview.y;

  set_result_insert(&async->in_progress.lua_previews, &work->super);

  log_trace("async_lua_preview %s", preview_path(pv).str);
  tpool_add_work(async->tpool, async_lua_preview_worker, work, true);
}
