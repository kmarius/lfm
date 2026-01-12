#include "lfm.h"

#include "../config.h"
#include "../hooks.h"
#include "../input.h"
#include "../macros.h"
#include "../mode.h"
#include "../search.h"
#include "../vec_env.h"
#include "auto/versiondef.h"
#include "private.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MODES_META "Lfm.Modes.Meta"
#define MODE_META "Lfm.Mode.Meta"
#define PROC_META "Lfm.Proc.Meta"

static int l_schedule(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);
  int delay = luaL_optinteger(L, 2, 0);
  if (delay < 0) {
    delay = 0;
  }
  lfm_schedule(lfm, lua_register_callback(L, 1), delay);
  return 0;
}

static int l_colors_clear(lua_State *L) {
  (void)L;
  config_colors_clear();
  ui_redraw(ui, REDRAW_FM);
  return 0;
}

static int l_handle_key(lua_State *L) {
  size_t len;
  const char *keys = luaL_checklstring(L, 1, &len);
  const char *end = keys + len;
  while (keys < end) {
    input_t u;
    int len = key_name_to_input(keys, &u);
    if (len < 0) {
      return luaL_error(L, "invalid key");
    }
    input_handle_key(lfm, u);
    keys += len;
  }
  return 0;
}

static int l_search(lua_State *L) {
  search(lfm, lua_tozsview(L, 1), true);
  return 0;
}

static int l_search_backwards(lua_State *L) {
  search(lfm, lua_tozsview(L, 1), false);
  return 0;
}

static int l_nohighlight(lua_State *L) {
  (void)L;
  search_nohighlight(lfm);
  return 0;
}

static int l_search_next(lua_State *L) {
  search_next(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_search_prev(lua_State *L) {
  search_prev(lfm, luaL_optbool(L, 1, false));
  return 0;
}

static int l_crash(lua_State *L) {
  xfree(L);
  return 0;
}

static int l_quit(lua_State *L) {
  return lua_quit(L, lfm);
}

static int l_print(lua_State *L) {
  int n = lua_gettop(L);
  lua_getglobal(L, "tostring");
  size_t bufsz = 128;
  char *buf = xcalloc(bufsz, 1);
  size_t ind = 0;
  for (int i = 1; i <= n; i++) {
    lua_pushvalue(L, -1);
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    size_t len;
    const char *s = lua_tolstring(L, -1, &len);
    if (s == NULL) {
      xfree(buf);
      return luaL_error(
          L, LUA_QL("tostring") " must return a string to " LUA_QL("print"));
    }
    if (i > 1) {
      buf[ind++] = '\t';
    }
    if (ind + len >= bufsz) {
      do {
        bufsz *= 2;
      } while (ind + len >= bufsz);
      buf = xrealloc(buf, bufsz);
    }
    strncpy(&buf[ind], s, len);
    ind += len;
    lua_pop(L, 1); /* pop result */
  }
  buf[ind++] = 0;
  ui_echom(ui, "%s", buf);
  xfree(buf);
  return 0;
}

static int l_error(lua_State *L) {
  ui_error(ui, "%s", luaL_optstring(L, 1, ""));
  return 0;
}

static int l_message_clear(lua_State *L) {
  (void)L;
  ui->show_message = false;
  ui_redraw(ui, REDRAW_CMDLINE);
  return 0;
}

// TODO: should we use a FILE* instead of fd?
struct proc {
  int pid;
  int fd;
};

static int l_proc_write(lua_State *L) {
  struct proc *proc = (struct proc *)lua_touserdata(L, 1);
  if (proc->fd == -1) {
    return luaL_error(L, "trying to write to closed stdin of process %d",
                      proc->pid);
  }

  size_t len;
  const char *buf = lua_tolstring(L, 2, &len);
  ssize_t n = write(proc->fd, buf, len);

  if (n == -1) {
    close(proc->fd);
    proc->fd = -1;
    return luaL_error(L, "write: %s", strerror(errno));
  }

  lua_pushnumber(L, n);
  return 1;
}

// proc:close and __gc
static int l_proc_close(lua_State *L) {
  struct proc *proc = (struct proc *)lua_touserdata(L, 1);
  if (proc->fd != -1) {
    close(proc->fd);
    proc->fd = -1;
  }
  return 0;
}

static int l_proc_send_signal(lua_State *L) {
  struct proc *proc = (struct proc *)lua_touserdata(L, 1);
  int sig = luaL_checkinteger(L, 2);
  lua_pushinteger(L, kill(proc->pid, sig));
  return 1;
}

static int l_proc_index(lua_State *L) {
  struct proc *proc = (struct proc *)lua_touserdata(L, 1);
  const char *key = luaL_checkstring(L, 2);

  // only field is "pid"
  if (streq(key, "pid")) {
    lua_pushinteger(L, proc->pid);
    return 1;
  }

  // refer everything else to the method table
  luaL_getmetatable(L, PROC_META);
  lua_getfield(L, -1, "__methods");
  lua_getfield(L, -1, key);

  return 1;
}

static int lua_proc_create(lua_State *L, int pid, int fd) {
  struct proc *proc = (struct proc *)lua_newuserdata(L, sizeof *proc);
  proc->pid = pid;
  proc->fd = fd;

  if (unlikely(luaL_newmetatable(L, PROC_META))) {
    lua_pushcfunction(L, l_proc_close);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, l_proc_index);
    lua_setfield(L, -2, "__index");

    lua_newtable(L);

    lua_pushcfunction(L, l_proc_write);
    lua_setfield(L, -2, "write");

    lua_pushcfunction(L, l_proc_close);
    lua_setfield(L, -2, "close");

    lua_pushcfunction(L, l_proc_send_signal);
    lua_setfield(L, -2, "send_signal");

    lua_setfield(L, -2, "__methods");
  }

  lua_setmetatable(L, -2);

  return 1;
}

static int l_spawn(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);

  // init just nulls these, we can exit without dropping, if nothing is added
  vec_str args = vec_str_init();
  vec_env env = vec_env_init();
  vec_bytes stdin_lines = vec_bytes_init();
  zsview working_directory = zsview_init();

  bool capture_stdout = false;
  bool capture_stderr = false;
  bool stdin_is_function = false;
  int stdin_fd = -1;
  int stdout_ref = 0;
  int stderr_ref = 0;
  int exit_ref = 0;

  luaL_checktype(L, 1, LUA_TTABLE); // [cmd, opts?]
  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
  }

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  vec_str_reserve(&args, n + 1);
  lua_read_vec_str(L, 1, &args);
  vec_str_push(&args, NULL);

  if (lua_gettop(L) == 2) {
    lua_getfield(L, 2, "stdin"); // [cmd, opts, opts.stdin]
    if (lua_isboolean(L, -1)) {
      stdin_is_function = lua_toboolean(L, -1);
    } else if (lua_isstring(L, -1)) {
      vec_bytes_push_back(&stdin_lines, lua_tobytes(L, -1));
    } else if (lua_istable(L, -1)) {
      lua_read_vec_bytes(L, -1, &stdin_lines);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_stdout"); // [cmd, opts, opts.on_stdout]
    if (lua_isfunction(L, -1)) {
      stdout_ref = lua_register_callback(L, -1); // [cmd, opts, opts.on_stdout]
    } else {
      capture_stdout = lua_toboolean(L, -1);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_stderr"); // [cmd, opts, opts.on_stderr]
    if (lua_isfunction(L, -1)) {
      stderr_ref = lua_register_callback(L, -1); // [cmd, opts, opts.on_stderr]
    } else {
      capture_stderr = lua_toboolean(L, -1);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_exit"); // [cmd, opts, opts.on_exit]
    if (lua_isfunction(L, -1)) {
      exit_ref = lua_register_callback(L, -1); // [cmd, opts, opts.on_exit]
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "env"); // [cmd, opts, opts.env]
    if (lua_istable(L, -1)) {
      for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
        // [cmd, opts, opts.env, key, val]

        // we make copies of these values because the luajit source code
        // suggests that, if value is not a string, gc may run and invalidate
        // other values that have been stored
        vec_env_push_back(
            &env, (struct env_entry){lua_tostrdup(L, -2), lua_tostrdup(L, -1)});
      }
    }
    lua_pop(L, 1); // [cmd, opts]
    lua_getfield(L, 2, "dir");
    if (!lua_isnil(L, -1)) {
      working_directory = lua_tozsview(L, -1);
    }
    lua_pop(L, 1);
  }

  int pid = lfm_spawn(lfm, args.data[0], args.data, &env,
                      !vec_bytes_is_empty(&stdin_lines) ? &stdin_lines : NULL,
                      stdin_is_function ? &stdin_fd : NULL, capture_stdout,
                      capture_stderr, stdout_ref, stderr_ref, exit_ref,
                      working_directory);

  vec_str_drop(&args);
  vec_env_drop(&env);
  vec_bytes_drop(&stdin_lines);

  if (pid != -1) {
    lua_proc_create(L, pid, stdin_fd);
    return 1;
  } else {
    lua_pushnil(L);
    lua_pushstring(L,
                   strerror(errno)); // not sure if something even sets errno
    return 2;
  }
}

static int l_execute(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);

  vec_str args = vec_str_init();
  vec_bytes stdout_lines = vec_bytes_init();
  vec_bytes stderr_lines = vec_bytes_init();
  vec_bytes stdin_lines = vec_bytes_init();
  vec_env env = vec_env_init();

  bool capture_stdout = false;
  bool capture_stderr = false;
  bool send_stdin = false;

  luaL_checktype(L, 1, LUA_TTABLE);
  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
  }

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  vec_str_reserve(&args, n + 1);
  lua_read_vec_str(L, 1, &args);
  vec_str_push(&args, NULL);

  if (lua_gettop(L) == 2) {
    // [cmd, opts]
    lua_getfield(L, 2, "stdin"); // [cmd, opts, opts.stdin]
    send_stdin = lua_toboolean(L, -1);
    if (lua_isstring(L, -1)) {
      vec_bytes_push_back(&stdin_lines, lua_tobytes(L, -1));
    } else if (lua_istable(L, -1)) {
      lua_read_vec_bytes(L, -1, &stdin_lines);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "capture_stdout"); //[cmd, opts, opts.capture_stdout]
    capture_stdout = lua_toboolean(L, -1);
    lua_pop(L, 1); //[cmd, opts]

    lua_getfield(L, 2, "capture_stderr"); //[cmd, opts, opts.capture_stderr]
    capture_stderr = lua_toboolean(L, -1);
    lua_pop(L, 1); //[cmd, opts]

    lua_getfield(L, 2, "env"); // [cmd, opts, opts.env]
    if (lua_istable(L, -1)) {
      for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
        // [cmd, opts, opts.env, key, val]
        vec_env_push_back(
            &env, (struct env_entry){lua_tostrdup(L, -2), lua_tostrdup(L, -1)});
      }
    }
    lua_pop(L, 1); // [cmd, opts]
  }

  int status = lfm_execute(lfm, args.data[0], args.data, &env,
                           send_stdin ? &stdin_lines : NULL,
                           capture_stdout ? &stdout_lines : NULL,
                           capture_stderr ? &stderr_lines : NULL);

  vec_env_drop(&env);
  vec_str_drop(&args);
  vec_bytes_drop(&stdin_lines);

  if (status < 0) {
    vec_bytes_drop(&stdout_lines);
    vec_bytes_drop(&stderr_lines);
    lua_pushnil(L);
    // not sure if something even sets errno
    lua_pushstring(L, strerror(errno));
    return 2;
  } else {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, status);
    lua_setfield(L, -2, "status");

    if (capture_stdout) {
      lua_push_vec_bytes(L, &stdout_lines);
      lua_setfield(L, -2, "stdout");
      vec_bytes_drop(&stdout_lines);
    }

    if (capture_stderr) {
      lua_push_vec_bytes(L, &stderr_lines);
      lua_setfield(L, -2, "stderr");
      vec_bytes_drop(&stderr_lines);
    }

    return 1;
  }
}

static int l_thread(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 3);

  int ref = 0;
  if (lua_type(L, 1) == LUA_TFUNCTION) {
    // try to string.dump the function and insert it at position 1
    // TODO: we could store a ref to encode
    if (lua_string_dump(L, 1)) {
      return lua_error(L);
    }
    lua_replace(L, 1);
  }
  luaL_checktype(L, 1, LUA_TSTRING);
  if (lua_gettop(L) > 1) {
    if (!lua_isnoneornil(L, 2)) {
      ref = lua_register_callback(L, 2);
    }
  }
  bytes chunk = lua_tobytes(L, 1);
  bytes arg = bytes_init();
  if (lua_gettop(L) == 3) {
    // encode optional argument
    if (lua_encode(L, 3, &arg)) {
      bytes_drop(&chunk);
      return lua_error(L);
    }
  }
  async_lua(&lfm->async, &chunk, &arg, ref);
  return 0;
}

static inline int map_key(lua_State *L, Trie *trie, bool allow_mode) {
  zsview keys = luaL_checkzsview(L, 1);

  if (!(lua_type(L, 2) == LUA_TFUNCTION || lua_isnil(L, 2))) {
    return luaL_argerror(L, 2, "expected function or nil");
  }

  zsview desc = {0};
  if (lua_type(L, 3) == LUA_TTABLE) {
    lua_getfield(L, 3, "desc");
    if (!lua_isnil(L, -1)) {
      desc = lua_tozsview(L, -1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 3, "mode");
    if (!lua_isnil(L, -1)) {
      if (!allow_mode) {
        return luaL_error(L, "mode not allowed here");
      }
      const hmap_modes_value *mode =
          hmap_modes_get(&lfm->modes, lua_tozsview(L, -1));
      if (mode == NULL) {
        return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
      }
      trie = mode->second.maps;
    }
    lua_pop(L, 1);
  }

  int ref = 0;
  if (!lua_isnil(L, 2)) {
    lua_pushvalue(L, 2);
    ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  int oldref;

  int status = input_map(trie, keys, ref, desc, &oldref);
  if (status < 0) {
    if (ref != 0) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    if (status == -2)
      return luaL_error(L, "key sequence too long");
    else
      return luaL_error(L, "malformed key sequence");
  }

  if (oldref) {
    luaL_unref(L, LUA_REGISTRYINDEX, oldref);
  }

  return 0;
}

static int l_map_key(lua_State *L) {
  return map_key(L, lfm->ui.maps.normal, true);
}

static int l_cmap_key(lua_State *L) {
  return map_key(L, lfm->ui.maps.input, false);
}

static int l_get_maps(lua_State *L) {
  zsview name = luaL_checkzsview(L, 1);
  const struct mode *mode = hmap_modes_at(&lfm->modes, name);
  if (!mode) {
    return luaL_error(L, "no such mode: %s", name);
  }
  bool prune = luaL_optbool(L, 2, false);
  vec_trie maps = trie_collect_leaves(mode->maps, prune);
  lua_createtable(L, vec_trie_size(&maps), 0);
  size_t i = 0;
  c_foreach(it, vec_trie, maps) {
    Trie *map = *it.ref;
    lua_createtable(L, 0, 3);
    lua_pushcstr(L, &map->desc);
    lua_setfield(L, -2, "desc");
    lua_pushcstr(L, &map->keys);
    lua_setfield(L, -2, "keys");
    lua_rawgeti(L, LUA_REGISTRYINDEX, map->ref);
    lua_setfield(L, -2, "f");
    lua_rawseti(L, -2, i + 1);
    i++;
  }
  vec_trie_drop(&maps);
  return 1;
}

static int l_current_mode(lua_State *L) {
  lua_pushcstr(L, &lfm->current_mode->name);
  return 1;
}

static int l_get_modes(lua_State *L) {
  lua_createtable(L, lfm->modes.size, 0);
  int i = 1;
  c_foreach_kv(_, v, hmap_modes, lfm->modes) {
    lua_pushcstr(L, &v->name);
    lua_rawseti(L, -2, i++);
  }
  return 1;
}

static int l_mode(lua_State *L) {
  if (lfm_mode_enter(lfm, luaL_checkzsview(L, 1)) != 0) {
    return luaL_error(L, "no such mode: %s", lua_tostring(L, -1));
  }
  return 0;
}

static int l_register_mode(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  struct mode mode = {0};

  lua_getfield(L, 1, "name");
  if (lua_isnil(L, -1)) {
    return luaL_error(L, "register_mode: missing field 'name'");
  }
  mode.name = cstr_from_zv(lua_tozsview(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, 1, "input");
  mode.is_input = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "prefix");
  mode.prefix = cstr_from_zv(lua_tozsview(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_enter");
  if (!lua_isnil(L, -1)) {
    mode.on_enter_ref = lua_register_callback(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_change");
  if (!lua_isnil(L, -1)) {
    mode.on_change_ref = lua_register_callback(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_return");
  if (!lua_isnil(L, -1)) {
    mode.on_return_ref = lua_register_callback(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_esc");
  if (!lua_isnil(L, -1)) {
    mode.on_esc_ref = lua_register_callback(L, -1);
  }
  lua_pop(L, 1);

  lua_getfield(L, 1, "on_exit");
  if (!lua_isnil(L, -1)) {
    mode.on_exit_ref = lua_register_callback(L, -1);
  }
  lua_pop(L, 1);

  if (lfm_mode_register(lfm, &mode) != 0) {
    return luaL_error(L, "mode \"%s\" already exists", mode.name);
  }

  return 0;
}

int l_register_hook(lua_State *L) {
  LUA_CHECK_ARGC(L, 2);
  const char *name = luaL_checkstring(L, 1);
  int id = hook_name_to_id(name);
  if (id == -1) {
    return luaL_error(L, "no such hook: %s", name);
  }
  id = lfm_add_hook(lfm, id, lua_register_callback(L, 2));
  lua_pushnumber(L, id);
  return 1;
}

int l_deregister_hook(lua_State *L) {
  int id = luaL_checknumber(L, 1);
  int ref = lfm_remove_hook(lfm, id);
  if (!ref) {
    return luaL_error(L, "no hook with id %d", id);
  }
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return 0;
}

static const struct luaL_Reg lfm_lib[] = {
    {"mode",            l_mode            },
    {"current_mode",    l_current_mode    },
    {"get_modes",       l_get_modes       },
    {"register_mode",   l_register_mode   },
    {"register_hook",   l_register_hook   },
    {"deregister_hook", l_deregister_hook },
    {"schedule",        l_schedule        },
    {"colors_clear",    l_colors_clear    },
    {"execute",         l_execute         },
    {"spawn",           l_spawn           },
    {"thread",          l_thread          },
    {"map",             l_map_key         },
    {"cmap",            l_cmap_key        },
    {"get_maps",        l_get_maps        },
    {"handle_key",      l_handle_key      },
    {"nohighlight",     l_nohighlight     },
    {"search",          l_search          },
    {"search_back",     l_search_backwards},
    {"search_next",     l_search_next     },
    {"search_prev",     l_search_prev     },
    {"crash",           l_crash           },
    {"error",           l_error           },
    {"message_clear",   l_message_clear   },
    {"quit",            l_quit            },
    {NULL,              NULL              },
};

static int l_modes_index(lua_State *L) {
  zsview key = luaL_checkzsview(L, 2);
  hmap_modes_value *md = hmap_modes_get_mut(&lfm->modes, key);
  if (md == NULL) {
    return 0;
  }

  struct mode **mode = lua_newuserdata(L, sizeof *mode);
  *mode = &md->second;
  luaL_newmetatable(L, MODE_META);
  lua_setmetatable(L, -2);

  return 1;
}

static int l_mode_index(lua_State *L) {
  struct mode *mode = *(struct mode **)luaL_checkudata(L, 1, MODE_META);
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "name")) {
    lua_pushcstr(L, &mode->name);
    return 1;
  } else if (streq(key, "prefix")) {
    lua_pushcstr(L, &mode->prefix);
    return 1;
  } else if (streq(key, "input")) {
    lua_pushboolean(L, mode->is_input);
    return 1;
  } else {
    return luaL_error(L, "no such field: %s", key);
  }
  return 0;
}

static int l_mode_newindex(lua_State *L) {
  struct mode *mode = *(struct mode **)luaL_checkudata(L, 1, MODE_META);
  const char *key = luaL_checkstring(L, 2);
  if (streq(key, "prefix")) {
    if (!mode->is_input) {
      return luaL_error(L, "can only set prefix for input modes");
    }
    zsview prefix = lua_tozsview(L, 3);
    cstr_assign_zv(&mode->prefix, prefix);
  } else {
    return luaL_error(L, "no such field: %s", key);
  }
  return 0;
}

static const struct luaL_Reg lfm_modes_mt[] = {
    {"__index", l_modes_index},
    {NULL,      NULL         }
};

static const struct luaL_Reg lfm_mode_mt[] = {
    {"__index",    l_mode_index   },
    {"__newindex", l_mode_newindex},
    {NULL,         NULL           }
};

int luaopen_lfm(lua_State *L) {
  lua_pushcfunction(L, l_print);
  lua_setglobal(L, "print");

  luaL_openlib(L, "lfm", lfm_lib, 0); // [lfm]

  luaopen_options(L);
  lua_setfield(L, -2, "o");

  luaopen_api(L);
  lua_setfield(L, -2, "api");

  luaopen_paths(L);
  lua_setfield(L, -2, "paths");

  luaopen_log(L);
  lua_setfield(L, -2, "log");

  luaopen_fn(L);
  lua_setfield(L, -2, "fn");

  luaopen_rifle(L);
  lua_setfield(L, -2, "rifle");

  luaL_newmetatable(L, MODE_META);
  luaL_register(L, NULL, lfm_mode_mt);
  lua_pop(L, 1);

  lua_newtable(L);
  luaL_newmetatable(L, MODES_META);
  luaL_register(L, NULL, lfm_modes_mt);
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "modes");

  lua_newtable(L);
  lua_pushzsview(L, c_zv(LFM_VERSION));
  lua_setfield(L, -2, "info");

  lua_pushzsview(L, c_zv(LFM_REVCOUNT));
  lua_setfield(L, -2, "revcount");

  lua_pushzsview(L, c_zv(LFM_COMMIT));
  lua_setfield(L, -2, "commit");

  lua_pushzsview(L, c_zv(LFM_BUILD_TYPE));
  lua_setfield(L, -2, "build_type");

  lua_pushzsview(L, c_zv(LFM_BRANCH));
  lua_setfield(L, -2, "branch");
  lua_setfield(L, -2, "version");

  lua_pop(L, 1); // []

  return 1;
}
