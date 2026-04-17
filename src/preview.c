#include "preview.h"

#include "cdims.h"
#include "config.h"
#include "defs.h"
#include "log.h"
#include "memory.h"
#include "ncutil.h"
#include "sha256.h"
#include "types/bytes.h"

#define STC_CSTR_IO
#include <stc/cstr.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>

// arbitrary upper bound for text previews
#define PREVIEW_MAX_BYTES (128 * 1024)

// return code interpretation of the previewer script taken from ranger
#define PREVIEW_DISPLAY_STDOUT 0
#define PREVIEW_NONE 1
#define PREVIEW_FILE_CONTENTS 2
#define PREVIEW_FIX_WIDTH 3
#define PREVIEW_FIX_HEIGHT 4
#define PREVIEW_FIX_WIDTH_AND_HEIGHT 5
#define PREVIEW_CACHE_AS_IMAGE 6
#define PREVIEW_AS_IMAGE 7

static void draw_text_preview(const Preview *p, struct ncplane *n);
static void update_text_preview(Preview *p, Preview *u);
static void destroy_text_preview(Preview *p);

static void draw_image_preview(const Preview *p, struct ncplane *n);
static void update_image_preview(Preview *p, Preview *u);
static void destroy_image_preview(Preview *p);

static inline Preview *preview_init(Preview *p, zsview path, i32 height,
                                    i32 width) {
  memset(p, 0, sizeof *p);
  p->path = cstr_from_zv(path);
  p->height = height;
  p->width = width;
  p->next_scheduled_load = 0;

  p->draw = draw_text_preview;
  p->update = update_text_preview;
  p->destroy = destroy_text_preview;
  p->status = PV_DELAYED;

  return p;
}

static inline Preview *preview_create(zsview path, i32 height, i32 width) {
  return preview_init(xmalloc(sizeof(Preview)), path, height, width);
}

static inline void destroy_preview(Preview *p) {
  assert(p->refcount == 0);
  cstr_drop(&p->path);
  xfree(p);
}

Preview *preview_inc_ref(Preview *Preview) {
  atomic_fetch_add(&Preview->refcount, 1);
  return Preview;
}

void preview_dec_ref(Preview *p) {
  assert(p->refcount > 0);
  if (unlikely(atomic_fetch_sub(&p->refcount, 1) == 1))
    preview_destroy(p);
}

static void destroy_text_preview(Preview *p) {
  bytes_drop(&p->data);
  destroy_preview(p);
}

Preview *preview_create_loading(zsview path, i32 height, i32 width) {
  Preview *p = preview_create(path, height, width);
  p->is_loading = true;
  return p;
}

static void update_text_preview(Preview *p, Preview *u) {
  bytes_drop(&p->data);
  p->data = u->data;
  p->mtime = u->mtime;
  p->width = u->width;
  p->height = u->height;
  p->is_loading = false;
  p->status = PV_LOADED;

  p->draw = u->draw;
  p->update = u->update;
  p->destroy = u->destroy;

  destroy_preview(u);
}

static void update_image_preview(Preview *p, Preview *u) {
  if (p->ncv) {
    ncvisual_destroy(p->ncv);
  }
  p->ncv = u->ncv;
  p->mtime = u->mtime;
  p->is_loading = false;
  p->width = u->width;
  p->height = u->height;
  p->status = PV_LOADED;

  destroy_preview(u);
}

static inline void data_from_stream(bytes *data, FILE *file, i32 max_lines) {
  char static_buf[4096];

  char *buf = static_buf;
  usize buf_sz = sizeof static_buf;

  usize size = fread(buf, 1, buf_sz, file);
  if (size == 0) {
    *data = bytes_init();
    return;
  }

  i32 num_lines = 0;
  const char *last_newline = NULL;
  for (usize i = 0; i < size; i++) {
    if (static_buf[i] == '\n') {
      num_lines++;
      last_newline = static_buf + i;
      if (num_lines == max_lines)
        break;
    }
  }

  if (size == buf_sz && num_lines < max_lines) {
    buf_sz *= 2;
    buf = malloc(buf_sz);
    memcpy(buf, static_buf, sizeof static_buf);
    last_newline = buf + (last_newline - static_buf);

    // more input
    for (;;) {
      usize i = size;
      size += fread(buf + size, 1, buf_sz - size, file);

      for (; i < size; i++) {
        if (buf[i] == '\n') {
          num_lines++;
          last_newline = buf + i;
          if (num_lines == max_lines)
            break;
        }
      }

      if (size < buf_sz)
        break; // no more output

      if (num_lines == max_lines)
        break;

      if (size > PREVIEW_MAX_BYTES) {
        log_error("previewer is sending too much data, stopping here");
        // make sure we backtrack to the last newline
        num_lines = max_lines;
        break;
      }

      buf_sz = buf_sz * 3 / 2;
      buf = realloc(buf, buf_sz);
    }
  }

  if (num_lines == max_lines)
    size = last_newline - buf;

  if (buf == static_buf)
    buf = memdup(static_buf, size);

  *data = (bytes){.buf = buf, .size = size};
}

// caller must pass a buffer of size PATH_MAX
static inline i32 gen_cache_path(zsview path, char *buf, usize buflen) {
  u8 hash[32];
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (u8 *)path.str, path.size);
  sha256_final(&ctx, hash);

  usize len = cstr_size(&cfg.cachedir);
  memcpy(buf, cstr_str(&cfg.cachedir), len);
  buf[len++] = '/';

  if (unlikely(len + 32 >= buflen))
    return -1;
  for (usize i = 0; i < 32; i++) {
    const char upper = hash[i] >> 4;
    const char lower = hash[i] & 0x0f;
    buf[len++] = lower < 10 ? '0' + lower : 'a' + lower - 10;
    buf[len++] = upper < 10 ? '0' + upper : 'a' + upper - 10;
  }
  buf[len] = 0;
  return 0;
}

// Set preview contents to an error message and log it
Preview *preview_error(Preview *p, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  bytes_drop(&p->data);
  char buf[128];
  i32 len = vsnprintf(buf, sizeof buf - 1, fmt, args);
  p->data = bytes_from_n(buf, len);
  log_error("%s", buf);
  va_end(args);
  return p;
}

Preview *preview_create_and_stat(zsview path, i32 height, i32 width) {
  Preview *p = preview_create(path, height, width);

  struct stat statbuf;
  if (stat(path.str, &statbuf) != -1)
    p->mtime = statbuf.st_mtime;

  return p;
}

// downscale images to fit the preview pane
static void downscale_image(struct ncvisual *ncv, i32 y, i32 x) {
  i32 cdimy = cdims.cdimy;
  i32 cdimx = cdims.cdimx;

  ncvgeom geom = {0};
  ncvisual_geom(NULL, ncv, NULL, &geom);

  f64 scaley = 1.0 * geom.pixy / (y * cdimy);
  f64 scale = 1.0 * geom.pixx / (x * cdimx);
  if (scaley > scale) {
    scale = scaley;
  }
  if (scale > 1.0) {
    i32 newy = (i32)(geom.pixy / scale);
    i32 newx = (i32)(geom.pixx / scale);
    log_trace("resizing: %d %d pane: %d %d", y * cdimy, x * cdimx, newy, newx);
    ncvisual_resize(ncv, newy, newx);
  }
}

Preview *preview_fork_previewer(zsview path, u32 width, u32 height,
                                i32 *pid_out, i32 fd[2]) {
  Preview *p = preview_create(path, height, width);

  if (cstr_is_empty(&cfg.previewer))
    return p;

  /* TODO: make proper use of the cache (on 2022-09-27) */
  /* we currently only use it as a place for the previewer to write to */
  char cache_path[PATH_MAX];
  if (cfg.preview_images) {
    if (unlikely(gen_cache_path(path, cache_path, sizeof cache_path) != 0))
      return preview_error(p, "gen_cache_path");

  } else {
    cache_path[0] = 0;
  }

  char width_str[32];
  char height_str[32];
  snprintf(width_str, sizeof width_str, "%u", width);
  snprintf(height_str, sizeof height_str, "%u", height);

  const char *args[7] = {
      cstr_str(&cfg.previewer),
      preview_path_str(p),
      width_str,
      height_str,
      cache_path,
      cfg.preview_images ? "True" : "False",
      NULL,
  };

  if (unlikely(pipe(fd) == -1)) {
    return preview_error(p, "pipe: ", strerror(errno));
  }

  i32 pid = fork();

  if (pid == 0) {
    // child

    // stdout
    close(fd[0]);
    dup2(fd[1], 1);
    close(fd[1]);

    // stderr (some program I can't remember didn't like closed stderr)
    i32 devnull = open("/dev/null", O_WRONLY | O_CREAT, 0666);
    dup2(devnull, 2);
    close(devnull);

    execv(args[0], (char **)args);
    log_perror("execv");
    _exit(ENOSYS);
  }

  close(fd[1]);

  if (unlikely(pid < 0)) {
    close(fd[0]);
    fd[0] = -1;
    return preview_error(p, "fork: ", strerror(errno));
  }

  *pid_out = pid;
  return p;
}

Preview *preview_read_output(Preview *p, i32 fd[2]) {
  struct stat statbuf;
  p->mtime = stat(cstr_str(&p->path), &statbuf) != -1 ? statbuf.st_mtime : 0;

  FILE *stdout = fdopen(fd[0], "r");
  if (unlikely(stdout == NULL)) {
    close(fd[0]);
    fd[0] = -1;
    return preview_error(p, "fdopen: %s", strerror(errno));
  }

  data_from_stream(&p->data, stdout, p->height);

  {
    char buf[4096];
    while (fread(buf, 1, sizeof buf, stdout) > 0) {
    }
  }

  fclose(stdout);
  fd[0] = -1;

  return p;
}

Preview *preview_handle_exit_status(Preview *p, i32 status) {

  // TODO: check other statuses?
  if (likely(WIFEXITED(status))) {
    i32 rc = WEXITSTATUS(status);
    switch (rc) {
    case PREVIEW_DISPLAY_STDOUT:
      break;
    case PREVIEW_NONE:
      break; // no preview
    case PREVIEW_FILE_CONTENTS: {
      FILE *fp_file = fopen(cstr_str(&p->path), "r");
      if (fp_file == NULL) {
        return preview_error(p, "fopen: ", strerror(errno));
      }
      bytes_drop(&p->data);
      data_from_stream(&p->data, fp_file, p->height);
      fclose(fp_file);
    } break;
    case PREVIEW_FIX_WIDTH:
      p->width = INT_MAX;
      break;
    case PREVIEW_FIX_HEIGHT:
      p->height = INT_MAX;
      break;
    case PREVIEW_FIX_WIDTH_AND_HEIGHT:
      p->width = INT_MAX;
      p->height = INT_MAX;
      break;
    case PREVIEW_CACHE_AS_IMAGE:
      if (cfg.preview_images) {

        // this is also done in p1
        char cache_path[PATH_MAX];
        if (cfg.preview_images) {
          if (gen_cache_path(cstr_zv(&p->path), cache_path,
                             sizeof cache_path) != 0) {
            return preview_error(p, "gen_cache_path");
          }
        } else {
          cache_path[0] = 0;
        }

        struct ncvisual *ncv = ncvisual_from_file(cache_path);
        if (unlikely(ncv == NULL)) {
          return preview_error(p, "ncvisual_from_file: ", strerror(errno));
        }
        bytes_drop(&p->data);
        downscale_image(ncv, p->height, p->width);
        p->ncv = ncv;
        p->draw = draw_image_preview;
        p->update = update_image_preview;
        p->destroy = destroy_image_preview;
      }
      break;
    case PREVIEW_AS_IMAGE:
      if (cfg.preview_images) {
        struct ncvisual *ncv = ncvisual_from_file(cstr_str(&p->path));
        if (unlikely(ncv == NULL)) {
          return preview_error(p, "ncvisual_from_file: ", strerror(errno));
        }
        bytes_drop(&p->data);
        downscale_image(ncv, p->height, p->width);
        p->ncv = ncv;
        p->draw = draw_image_preview;
        p->update = update_image_preview;
        p->destroy = destroy_image_preview;
      }
      break;
    default:
      return preview_error(p, "previewer returned %d", rc);
    }
  }

  return p;
}

static void draw_text_preview(const Preview *p, struct ncplane *n) {
  ncplane_erase(n);

  if (bytes_is_empty(p->data))
    return;

  u32 nrow;
  ncplane_dim_yx(n, &nrow, NULL);

  const char *pos = p->data.buf;
  isize remaining = p->data.size;

  for (i32 i = 0; i < (i32)nrow && remaining > 0; i++) {
    ncplane_cursor_move_yx(n, i, 0);
    ncplane_set_fg_default(n);
    ncplane_set_bg_default(n);
    ncplane_set_styles(n, NCSTYLE_NONE);

    const char *end = memchr(pos, '\n', remaining);
    if (end == NULL)
      end = pos + remaining;

    ncplane_putcs_ansi(n, c_sv(pos, end - pos));

    remaining -= end - pos + 1;
    pos = end + 1;
  }

  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
}

static void draw_image_preview(const Preview *p, struct ncplane *n) {
  ncplane_erase(n);

  if (!p->ncv)
    return;
  struct ncvisual_options vopts = {
      .scaling = NCSCALE_SCALE,
      .n = n,
      .blitter = NCBLIT_PIXEL,
  };
  if (ncvisual_blit(ncplane_notcurses(n), p->ncv, &vopts) == NULL) {
    log_error("ncvisual_blit");
  }
}

static void destroy_image_preview(Preview *p) {
  if (p->ncv) {
    ncvisual_destroy(p->ncv);
    p->ncv = NULL;
  }
  destroy_preview(p);
}
