#include "cleanup.h"
#include "config.h"
#include "defs.h"
#include "lfmlua.h"
#include "log.h"
#include "loop.h"
#include "private.h"
#include "stc/zsview.h"
#include "types/vec_bytes.h"
#include "types/vec_env.h"

#include <ev.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define PROC_META "Lfm.Proc.Meta"

declare_dlist(list_child, struct child_watcher);
static list_child child_watchers;

static void list_child_drop(const list_child *);
static void child_output_cb(EV_P_ ev_io *w, int revents);
static inline void destroy_child_watcher(struct child_watcher *w);

static void deinit() {
  list_child_drop(&child_watchers);
}

__attribute__((constructor)) static void init() {
  add_dtor(deinit);
}

// ev_io wrapper for stdout/stderr
struct io_watcher {
  ev_io watcher;
  FILE *stream;
  int ref; // lua ref to callback
};

// ev_child wrapper for child processes with stdout/err
struct child_watcher {
  ev_child w;
  struct io_watcher wstdout; // valid if .stream != NULL
  struct io_watcher wstderr; // valid if .stream != NULL
  int ref;                   // ref to lua callback
};

#define i_declared
#define i_type list_child, struct child_watcher
#define i_keydrop(p) (destroy_child_watcher(p))
#define i_no_clone
#include "stc/dlist.h"

static void init_io_watcher(struct io_watcher *w, Lfm *lfm, int fd, int ref) {
  if (fd == -1)
    return;

  FILE *stream = fdopen(fd, "r");
  if (stream == NULL) {
    log_error("fdopen: %s", strerror(errno));
    close(fd);
    return;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_error("fcntl");
    fclose(stream);
    return;
  }

  w->watcher.data = lfm;
  w->stream = stream;
  w->ref = ref;

  ev_io_init(&w->watcher, child_output_cb, fd, EV_READ);
  ev_io_start(event_loop, &w->watcher);
}

// watcher and corresponding stdout/-err watchers need to be stopped before
// calling this function
static inline void destroy_child_watcher(struct child_watcher *w) {
  if (!w)
    return;
  Lfm *lfm = w->w.data;
  if (w->wstdout.stream) {
    if (w->wstdout.ref)
      lfm_lua_child_stdout_cb(lfm->L, w->wstdout.ref, NULL, 0);
    fclose(w->wstdout.stream);
  }
  if (w->wstderr.stream) {
    if (w->wstderr.ref)
      lfm_lua_child_stdout_cb(lfm->L, w->wstderr.ref, NULL, 0);
    fclose(w->wstderr.stream);
  }
}

static void child_exit_cb(EV_P_ ev_child *w, int revents) {
  (void)revents;

  struct child_watcher *child = (struct child_watcher *)w;
  Lfm *lfm = w->data;

  if (child->wstdout.stream) {
    ev_invoke(EV_A_ & child->wstdout.watcher, 0);
    ev_io_stop(EV_A_ & child->wstdout.watcher);
  }

  if (child->wstderr.stream) {
    ev_invoke(EV_A_ & child->wstderr.watcher, 0);
    ev_io_stop(EV_A_ & child->wstderr.watcher);
  }

  if (child->ref) {
    int status;
    if (WIFSIGNALED(w->rstatus)) {
      status = 128 + WTERMSIG(w->rstatus);
    } else {
      status = WEXITSTATUS(w->rstatus);
    }
    lfm_lua_child_exit_cb(lfm->L, child->ref, status);
  }

  ev_child_stop(EV_A_ w);

  list_child_erase_node(&child_watchers, list_child_get_node(child));
}

static void child_output_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;

  struct io_watcher *data = (struct io_watcher *)w;
  Lfm *lfm = w->data;

  char *line = NULL;
  usize n = 0;
  isize read;
  while ((read = getline(&line, &n, data->stream)) != -1) {
    if (line[read - 1] == '\n')
      read--;

    if (data->ref) {
      lfm_lua_child_stdout_cb(lfm->L, data->ref, line, read);
    } else {
      lfm_printf(lfm, "%s", line);
    }
  }
  xfree(line);

  // this prevents the callback being immediately called again by libev
  if (errno == EAGAIN)
    clearerr(data->stream);
}

// lua userdata to represent a spawned process
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

  usize len;
  const char *buf = lua_tolstring(L, 2, &len);
  isize n = write(proc->fd, buf, len);

  if (n == -1) {
    close(proc->fd);
    proc->fd = -1;
    return luaL_error(L, "write: %s", strerror(errno));
  }

  log_debug("write %d bytes to %d", n, proc->fd);

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

struct spawn_opts {
  vec_str args;             // program arguments
  vec_env env;              // environment overrides
  zsview working_directory; // working directory for the child
  bool pipe_stdin;          // open a pipe to the child's stdin
  vec_bytes stdin_data;     // data to send to the child's stdin
  bool keep_stdin_open;     // don't close stdin pipe so more input can be sent
  bool capture_stdout;      // connect a pipe to child's stdout
  bool capture_stderr;      // connect a pipe to child's stderr
  int stdout_ref;           // lua callback for stdout
  int stderr_ref;           // lua callback for stderr
  int exit_ref;             // lua callback on child exit
};

static inline void close_pipe_safe(int fd[2]) {
  if (fd[0] > 0)
    close(fd[0]);
  if (fd[1] > 0)
    close(fd[1]);
}

static inline int spawn(const struct spawn_opts *data, int *stdin_fd) {
  int pipe_stdin[2] = {-1, -1};
  int pipe_stdout[2] = {-1, -1};
  int pipe_stderr[2] = {-1, -1};

  int status = 0;

  if (data->pipe_stdin)
    status |= pipe(pipe_stdin);
  if (data->capture_stdout)
    status |= pipe(pipe_stdout);
  if (data->capture_stderr)
    status |= pipe(pipe_stderr);

  if (unlikely(status != 0)) {
    lfm_errorf(lfm, "pipe: %s", strerror(errno));
    goto fail;
  }

  int pid = fork();
  if (unlikely(pid < 0)) {
    lfm_errorf(lfm, "fork: %s", strerror(errno));
    goto fail;
  }

  if (pid == 0) {
    // child

    c_foreach(n, vec_env, cfg.extra_env) {
      vec_env_raw v = vec_env_value_toraw(n.ref);
      setenv(v.key, v.val, 1);
    }
    c_foreach(n, vec_env, data->env) {
      vec_env_raw v = vec_env_value_toraw(n.ref);
      setenv(v.key, v.val, 1);
    }

    if (data->pipe_stdin) {
      close(pipe_stdin[1]);
      dup2(pipe_stdin[0], 0);
      close(pipe_stdin[0]);
    }

    if (data->capture_stdout) {
      close(pipe_stdout[0]);
      dup2(pipe_stdout[1], 1);
      close(pipe_stdout[1]);
    } else {
      int devnull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
      dup2(devnull, 1);
      close(devnull);
    }

    if (data->capture_stderr) {
      close(pipe_stderr[0]);
      dup2(pipe_stderr[1], 2);
      close(pipe_stderr[1]);
    } else {
      int devnull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
      dup2(devnull, 2);
      close(devnull);
    }

    if (!zsview_is_empty(data->working_directory)) {
      if (chdir(data->working_directory.str) != 0) {
        if (data->capture_stderr) {
          char buf[128];
          i32 len = snprintf(buf, sizeof buf - 1, "chdir: %s", strerror(errno));
          write(2, buf, len);
        }
        _exit(1);
      }
    }

    execvp(data->args.data[0], (char **)data->args.data);
    log_error("execvp: %s", strerror(errno));
    if (data->capture_stderr) {
      char buf[128];
      i32 len = snprintf(buf, sizeof buf - 1, "execvp: %s", strerror(errno));
      write(2, buf, len);
    }
    _exit(ENOSYS);
  }

  // prepare child watcher with io watchers for stdout and stderr
  if (data->exit_ref != 0 || data->capture_stdout || data->capture_stderr) {
    struct child_watcher *watcher =
        list_child_push(&child_watchers, (struct child_watcher){
                                             .ref = data->exit_ref,
                                         });
    if (data->capture_stdout) {
      close(pipe_stdout[1]);
      init_io_watcher(&watcher->wstdout, lfm, pipe_stdout[0], data->stdout_ref);
    }
    if (data->capture_stderr) {
      close(pipe_stderr[1]);
      init_io_watcher(&watcher->wstderr, lfm, pipe_stderr[0], data->stderr_ref);
    }
    ev_child_init(&watcher->w, child_exit_cb, pid, 0);
    watcher->w.data = lfm;
    ev_child_start(event_loop, &watcher->w);
  }

  // send input and close/return fd if needed
  if (data->pipe_stdin) {
    close(pipe_stdin[0]);
    c_foreach(it, vec_bytes, data->stdin_data) {
      write(pipe_stdin[1], it.ref->buf, it.ref->size);
    }
    if (stdin_fd && data->keep_stdin_open) {
      *stdin_fd = pipe_stdin[1];
    } else {
      close(pipe_stdin[1]);
    }
  }

  return pid;

fail:
  close_pipe_safe(pipe_stdin);
  close_pipe_safe(pipe_stdout);
  close_pipe_safe(pipe_stderr);

  return -1;
}

int l_spawn(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);

  struct spawn_opts opts = {};

  luaL_checktype(L, 1, LUA_TTABLE); // [cmd, opts?]
  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
  }

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  vec_str_reserve(&opts.args, n + 1);
  lua_read_vec_str(L, 1, &opts.args);
  vec_str_push(&opts.args, NULL);

  if (lua_gettop(L) == 2) {
    lua_getfield(L, 2, "stdin"); // [cmd, opts, opts.stdin]
    if (lua_isboolean(L, -1)) {
      bool val = lua_toboolean(L, -1);
      opts.keep_stdin_open = val;
      opts.pipe_stdin = val;
    } else if (lua_isstring(L, -1)) {
      lua_read_bytes_into_chunks(L, -1, &opts.stdin_data);
      opts.pipe_stdin = true;
    } else if (lua_istable(L, -1)) {
      lua_read_vec_bytes_into_chunks(L, -1, &opts.stdin_data);
      opts.pipe_stdin = true;
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_stdout"); // [cmd, opts, opts.on_stdout]
    if (lua_isfunction(L, -1)) {
      opts.stdout_ref =
          lua_register_callback(L, -1); // [cmd, opts, opts.on_stdout]
      opts.capture_stdout = true;
    } else {
      opts.capture_stdout = lua_toboolean(L, -1);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_stderr"); // [cmd, opts, opts.on_stderr]
    if (lua_isfunction(L, -1)) {
      opts.stderr_ref =
          lua_register_callback(L, -1); // [cmd, opts, opts.on_stderr]
      opts.capture_stderr = true;
    } else {
      opts.capture_stderr = lua_toboolean(L, -1);
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "on_exit"); // [cmd, opts, opts.on_exit]
    if (lua_isfunction(L, -1)) {
      opts.exit_ref = lua_register_callback(L, -1); // [cmd, opts, opts.on_exit]
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "env"); // [cmd, opts, opts.env]
    if (lua_istable(L, -1)) {
      for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
        // [cmd, opts, opts.env, key, val]

        // we make copies of these values because the luajit source code
        // suggests that, if value is not a string, gc may run and invalidate
        // other values that have been stored
        vec_env_push_back(&opts.env, (struct env_entry){lua_tostrdup(L, -2),
                                                        lua_tostrdup(L, -1)});
      }
    }
    lua_pop(L, 1); // [cmd, opts]
    lua_getfield(L, 2, "dir");
    if (!lua_isnil(L, -1)) {
      opts.working_directory = lua_tozsview(L, -1);
    }
    lua_pop(L, 1);
  }

  int stdin_fd = -1;
  int pid = spawn(&opts, &stdin_fd);

  vec_str_drop(&opts.args);
  vec_env_drop(&opts.env);
  vec_bytes_drop(&opts.stdin_data);

  if (pid == -1) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  lua_proc_create(L, pid, stdin_fd);
  return 1;
}
