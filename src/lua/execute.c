#include "config.h"
#include "defs.h"
#include "private.h"
#include "stc/zsview.h"
#include "types/bytes.h"
#include "types/vec_bytes.h"
#include "types/vec_env.h"

#include <ev.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

struct execute_opts {
  vec_str args;             // program arguments
  vec_env env;              // environment overrides
  zsview working_directory; // working directory for the child
  bool pipe_stdin;          // open a pipe to the child's stdin
  vec_bytes stdin_data;     // data to send to the child's stdin
  bool capture_stdout;      // connect a pipe to child's stdout
  bool capture_stderr;      // connect a pipe to child's stderr
};

static inline void close_pipe_safe(int fd[2]) {
  if (fd[0] > 0)
    close(fd[0]);
  if (fd[1] > 0)
    close(fd[1]);
}

// TODO: we could avoid all the output buffering and allocations
// by getting it straight to the lua state
// TODO: we should just take the raw output and split by newlines when we
// push the result into the lua state

// execute a foreground program, returns negative value if there command
// was an error during init or fork, leaves output/error untouched
static inline int execute(const struct execute_opts *data,
                          vec_bytes *stdout_data, vec_bytes *stderr_data) {
  int rc, status = 0;

  lfm_run_hook(lfm, LFM_HOOK_EXECPRE);
  ev_signal_stop(lfm->loop, &lfm->sigint_watcher);
  ev_signal_stop(lfm->loop, &lfm->sigtstp_watcher);
  ui_suspend(&lfm->ui);

  int pipe_stdin[2] = {-1, -1};
  int pipe_stdout[2] = {-1, -1};
  int pipe_stderr[2] = {-1, -1};

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

    signal(SIGINT, SIG_DFL);
    if (chdir(cstr_str(&lfm->fm.pwd)) != 0) {
      fprintf(stderr, "chdir: %s\n", strerror(errno));
      _exit(1);
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
    }

    if (data->capture_stderr) {
      close(pipe_stderr[0]);
      dup2(pipe_stderr[1], 2);
      close(pipe_stderr[1]);
    }

    execvp(data->args.data[0], (char *const *)data->args.data);
    log_error("execvp: %s", strerror(errno));
    if (data->capture_stderr) {
      char buf[128];
      i32 len = snprintf(buf, sizeof buf - 1, "execvp: %s", strerror(errno));
      write(2, buf, len);
    }
    _exit(127);
  }

  signal(SIGINT, SIG_IGN);
  FILE *file_stderr = NULL;
  FILE *file_stdout = NULL;

  if (data->capture_stdout) {
    close(pipe_stdout[1]);

    i32 fd = pipe_stdout[0];
    i32 flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    file_stdout = fdopen(pipe_stdout[0], "r");
    if (file_stdout == NULL) {
      log_error("fdopen: %s", strerror(errno));
      close(pipe_stdout[0]);
    }
  }

  if (data->capture_stderr) {
    close(pipe_stderr[1]);

    int fd = pipe_stderr[0];
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    file_stderr = fdopen(pipe_stderr[0], "r");
    if (file_stderr == NULL) {
      log_error("fdopen: %s", strerror(errno));
      close(pipe_stderr[0]);
    }
  }

  if (data->pipe_stdin) {
    close(pipe_stdin[0]);
    int fd = pipe_stdin[1];
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  // TODO: should we support binary (not newline delimited) output
  if (data->capture_stdout || data->capture_stderr || data->pipe_stdin) {
    struct pollfd pfds[2] = {0};

    vec_bytes_iter it = {0};
    if (data->pipe_stdin)
      it = vec_bytes_begin(&data->stdin_data);

    if (data->capture_stdout) {
      pfds[0].fd = fileno(file_stdout);
      pfds[0].events = POLLIN;
    }
    if (data->capture_stderr) {
      pfds[1].fd = fileno(file_stderr);
      pfds[1].events = POLLIN;
    }

    FILE *files[2] = {
        file_stdout,
        file_stderr,
    };
    vec_bytes *vecs[2] = {
        // we are not polling the respective fds if these are NULL
        stdout_data,
        stderr_data,
    };

    // TODO: we need a dynamic buffer type
    usize bufsz[] = {512, 512};
    char *buf[] = {NULL, NULL};
    usize buf_idx[] = {0, 0};

    bool sending_input = data->pipe_stdin;

    char *line = NULL;
    usize n;
    i32 num_open_fds = data->capture_stdout + data->capture_stderr;
    while (num_open_fds > 0 || sending_input) {
      if (sending_input) {
        // we can not write arbitrarily large data here because the pipes have
        // limited size. we don't chunk the data here, it is already chunked in
        // the vectors
        if (it.ref != NULL) {
          if (write(pipe_stdin[1], it.ref->buf, it.ref->size) == -1) {
            if (waitpid(pid, &status, WNOHANG) == -1) {
              // child died
              sending_input = false;
            }
          } else {
            vec_bytes_next(&it);
          }
        }
        if (it.ref == NULL) {
          close(pipe_stdin[1]);
          pipe_stdin[1] = -1;
          sending_input = false;
        }
      }

      if (poll(pfds, 2, -1) == -1) {
        if (errno == EINTR)
          continue;
        log_error("poll: %s", strerror(errno));
        break;
      }

      for (i32 i = 0; i < 2; i++) {
        if (pfds[i].revents != 0) {
          if (pfds[i].revents & POLLIN) {
            i32 read;
            while ((read = getline(&line, &n, files[i])) != -1) {
              if (unlikely(line[read - 1] != '\n')) {
                // fragment of a line, buffer it
                // this happens rarely and I haven't found out why, yet
                if (unlikely(buf[i] == NULL)) {
                  while ((i32)bufsz[i] < read * 2)
                    bufsz[i] *= 2;
                  buf[i] = malloc(bufsz[i]);
                }
                if (buf_idx[i] + read > bufsz[i]) {
                  while (buf_idx[i] + read > bufsz[i])
                    bufsz[i] *= 2;
                  buf[i] = realloc(buf[i], bufsz[i]);
                }
                memcpy(buf[i] + buf_idx[i], line, read);
                buf_idx[i] += read;
                continue;
              }
              read--;
              if (unlikely(buf_idx[i] > 0)) {
                // last part of a fragmented line
                bytes bytes = bytes_init();
                bytes.size = buf_idx[i] + read;
                if (buf_idx[i] + read > bufsz[i]) {
                  bytes.buf = malloc(buf_idx[i] + read);
                  memcpy(bytes.buf, buf[i], buf_idx[i]);
                } else {
                  bytes.buf = buf[i];
                  buf[i] = NULL;
                }
                memcpy(bytes.buf + buf_idx[i], line, read);
                vec_bytes_push_back(vecs[i], bytes);
                buf_idx[i] = 0;
              } else {
                vec_bytes_push_back(vecs[i], bytes_from_n(line, read));
              }
            }
            // if we don't clear this error poll will indicate the file is
            // readable and we will loop forever
            if (errno == EAGAIN)
              clearerr(files[i]);
          } else { /* POLLERR | POLLHUP */
            pfds[i].fd = -1;
            num_open_fds--;
          }
        }
      }
    }

    if (pipe_stdin[1] > 0)
      close(pipe_stdin[1]);
    if (data->capture_stdout)
      fclose(file_stdout);
    if (data->capture_stderr)
      fclose(file_stderr);
    free(line);

    // if the output didn't end in \n we might have a fragment in the buffer
    if (buf_idx[0] > 0)
      vec_bytes_push_back(vecs[0], bytes_from_n(buf[0], buf_idx[0]));
    if (buf_idx[1] > 0)
      vec_bytes_push_back(vecs[1], bytes_from_n(buf[1], buf_idx[1]));
    free(buf[0]);
    free(buf[1]);
  }

  log_trace("waiting for process %d to finish", pid);
  do {
    rc = waitpid(pid, &status, 0);
  } while ((rc == -1) && (errno == EINTR));

  int rstatus;
  if (WIFSIGNALED(status)) {
    rstatus = 128 + WTERMSIG(status);
  } else {
    rstatus = WEXITSTATUS(status);
  }
  log_trace("child %d finished with status %d", pid, rstatus);

resume:
  ui_resume(&lfm->ui);
  ev_signal_start(lfm->loop, &lfm->sigint_watcher);
  ev_signal_start(lfm->loop, &lfm->sigtstp_watcher);
  lfm_run_hook(lfm, LFM_HOOK_EXECPOST);

  return rstatus;

fail:
  close_pipe_safe(pipe_stdin);
  close_pipe_safe(pipe_stdout);
  close_pipe_safe(pipe_stderr);
  rstatus = -1;
  goto resume;
}

int l_execute(lua_State *L) {
  LUA_CHECK_ARGMAX(L, 2);

  struct execute_opts opts = {};

  luaL_checktype(L, 1, LUA_TTABLE);
  if (lua_gettop(L) == 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
  }

  const int n = lua_objlen(L, 1);
  luaL_argcheck(L, n > 0, 1, "no command given");

  vec_str_reserve(&opts.args, n + 1);
  lua_read_vec_str(L, 1, &opts.args);
  vec_str_push(&opts.args, NULL);

  if (lua_gettop(L) == 2) {
    // [cmd, opts]
    lua_getfield(L, 2, "stdin"); // [cmd, opts, opts.stdin]
    if (lua_isstring(L, -1)) {
      lua_read_bytes_into_chunks(L, -1, &opts.stdin_data);
      opts.pipe_stdin = true;
    } else if (lua_istable(L, -1)) {
      lua_read_vec_bytes_into_chunks(L, -1, &opts.stdin_data);
      log_debug("%lu", vec_bytes_size(&opts.stdin_data));
      opts.pipe_stdin = true;
    }
    lua_pop(L, 1); // [cmd, opts]

    lua_getfield(L, 2, "capture_stdout"); //[cmd, opts, opts.capture_stdout]
    opts.capture_stdout = lua_toboolean(L, -1);
    lua_pop(L, 1); //[cmd, opts]

    lua_getfield(L, 2, "capture_stderr"); //[cmd, opts, opts.capture_stderr]
    opts.capture_stderr = lua_toboolean(L, -1);
    lua_pop(L, 1); //[cmd, opts]

    lua_getfield(L, 2, "env"); // [cmd, opts, opts.env]
    if (lua_istable(L, -1)) {
      for (lua_pushnil(L); lua_next(L, -2) != 0; lua_pop(L, 1)) {
        // [cmd, opts, opts.env, key, val]
        vec_env_push_back(&opts.env, (struct env_entry){lua_tostrdup(L, -2),
                                                        lua_tostrdup(L, -1)});
      }
    }
    lua_pop(L, 1); // [cmd, opts]
  }

  vec_bytes stdout_data = vec_bytes_init();
  vec_bytes stderr_data = vec_bytes_init();
  int status = execute(&opts, &stdout_data, &stderr_data);

  vec_env_drop(&opts.env);
  vec_str_drop(&opts.args);
  vec_bytes_drop(&opts.stdin_data);

  if (status < 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_createtable(L, 0, 4);
  lua_pushnumber(L, status);
  lua_setfield(L, -2, "status");

  if (opts.capture_stdout) {
    lua_push_vec_bytes(L, &stdout_data);
    lua_setfield(L, -2, "stdout");
  }

  if (opts.capture_stderr) {
    lua_push_vec_bytes(L, &stderr_data);
    lua_setfield(L, -2, "stderr");
  }

  vec_bytes_drop(&stdout_data);
  vec_bytes_drop(&stderr_data);
  return 1;
}
