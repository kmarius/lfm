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

struct lua_preview_work {
  struct result super;
  struct async_ctx *async;
  Preview *preview;
  bytes chunk; // lua code to execute
  int width;
  int height;
  Preview *update;
};

static void destroy(void *p) {
  struct lua_preview_work *work = p;
  bytes_drop(&work->chunk);
  preview_destroy(work->update);
  preview_dec_ref(work->preview);
  free(work);
}

static void callback(void *p, Lfm *lfm) {
  struct lua_preview_work *work = p;
  preview_update(work->preview, work->update);
  work->update = NULL;
  ui_redraw(&lfm->ui, REDRAW_PREVIEW);
  set_result_erase(&lfm->async.in_progress.lua_previews, p);
}

static void worker(void *arg) {
  struct lua_preview_work *work = arg;

  Preview *pv = preview_create_and_stat(preview_path(work->preview),
                                        work->height, work->width);
  work->update = pv;

  if (unlikely(L_thread == NULL)) {
    if (L_thread_init()) {
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
  submit_async_result(work->async, (struct result *)work);
  if (likely(L_thread != NULL))
    lua_gc(L_thread, LUA_GCCOLLECT, 0); // collectgarbage("collect")

  return;

err:
  // nothing, currently
  goto end;
}

void async_lua_preview(struct async_ctx *async, struct Preview *pv) {
  struct lua_preview_work *work = xcalloc(1, sizeof *work);
  work->super.callback = &callback;
  work->super.destroy = &destroy;

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
  tpool_add_work(async->tpool, worker, work, true);
}
