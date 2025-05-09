#include "preview.h"

#include "config.h"
#include "log.h"
#include "memory.h"
#include "ncutil.h"
#include "popen_arr.h"
#include "sha256.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PREVIEW_MAX_LINE_LENGTH 1024 // includes escapes and color codes

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

static inline Preview *preview_init(Preview *p, const char *path, int height,
                                    int width) {
  memset(p, 0, sizeof *p);
  p->path = strdup(path);
  p->reload_height = height;
  p->reload_width = width;
  p->next = current_millis();

  p->draw = draw_text_preview;
  p->update = update_text_preview;
  p->destroy = destroy_text_preview;
  p->loading = PV_LOADING_DELAYED;

  return p;
}

static inline Preview *preview_create(const char *path, int height, int width) {
  return preview_init(xmalloc(sizeof(Preview)), path, height, width);
}

static void destroy_text_preview(Preview *p) {
  vec_str_drop(&p->lines);
  xfree(p->path);
  xfree(p);
}

Preview *preview_create_loading(const char *path, int height, int width) {
  Preview *p = preview_create(path, height, width);
  p->loading = true;
  return p;
}

static void update_text_preview(Preview *p, Preview *u) {
  vec_str_drop(&p->lines);
  p->lines = u->lines;
  p->mtime = u->mtime;
  p->reload_width = u->reload_width;
  p->reload_height = u->reload_height;
  p->loadtime = u->loadtime;
  p->loading = false;
  p->status = PV_LOADING_NORMAL;

  p->draw = u->draw;
  p->update = u->update;
  p->destroy = u->destroy;

  xfree(u->path);
  xfree(u);
}

static void update_image_preview(Preview *p, Preview *u) {
  if (p->ncv) {
    ncvisual_destroy(p->ncv);
  }
  p->ncv = u->ncv;
  p->mtime = u->mtime;
  p->loadtime = u->loadtime;
  p->loading = false;
  p->reload_width = u->reload_width;
  p->reload_height = u->reload_height;
  p->status = PV_LOADING_NORMAL;

  xfree(u->path);
  xfree(u);
}

// like fgets, but seeks to the next line if dest is full.
static inline char *fgets_seek(char *dest, int n, FILE *fp) {
  int c = 0;
  char *cs = dest;

  while (--n > 0 && (c = getc(fp)) != EOF) {
    if ((*cs++ = c) == '\n') {
      break;
    }
  }

  if (c != EOF && c != '\n') {
    while ((c = getc(fp)) != EOF && c != '\n') {
    }
  }

  *cs = 0;
  return (c == EOF && cs == dest) ? NULL : dest;
}

// caller must should probably just pass a buffer of size PATH_MAX
static inline void gen_cache_path(char *cache_path, const char *path) {
  uint8_t buf[32];
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (uint8_t *)path, strlen(path));
  sha256_final(&ctx, buf);
  cache_path += sprintf(cache_path, "%s/", cfg.cachedir);
  for (size_t i = 0; i < 32; i++) {
    const char upper = buf[i] >> 4;
    const char lower = buf[i] & 0x0f;
    cache_path[2 * i] = lower < 10 ? '0' + lower : 'a' + lower - 10;
    cache_path[2 * i + 1] = upper < 10 ? '0' + upper : 'a' + upper - 10;
  }
  cache_path[2 * 32] = 0;
}

Preview *preview_create_from_file(const char *path, uint32_t width,
                                  uint32_t height) {
  char buf[PREVIEW_MAX_LINE_LENGTH];

  Preview *p = preview_create(path, height, width);
  p->loadtime = current_millis();

  struct stat statbuf;
  p->mtime = stat(path, &statbuf) != -1 ? statbuf.st_mtime : 0;

  if (!cfg.previewer) {
    return p;
  }

  /* TODO: make proper use of the cache (on 2022-09-27) */
  /* we currently only use it as a place for the previewer to write to */
  char cache_path[PATH_MAX];
  if (cfg.preview_images) {
    gen_cache_path(cache_path, path);
  } else {
    cache_path[0] = 0;
  }

  char w[32];
  char h[32];
  snprintf(w, sizeof w, "%u", width);
  snprintf(h, sizeof h, "%u", height);

  const char *args[7] = {cfg.previewer,
                         p->path,
                         w,
                         h,
                         cache_path,
                         cfg.preview_images ? "True" : "False",
                         NULL};

  FILE *fp = NULL;
  int pid = popen2_arr_p(NULL, &fp, NULL, args[0], (char *const *)args, NULL);
  if (!fp) {
    vec_str_emplace(&p->lines, strerror(errno));
    log_error("popen2_arr_p: %s", strerror(errno));
    return p;
  }

  for (uint32_t i = 0; i < height && fgets_seek(buf, sizeof buf, fp); i++) {
    vec_str_emplace(&p->lines, buf);
  }
  while (getc(fp) != EOF) {
  }

  // if we try to close the pipe (so that the child exits), the process gets
  // reaped by libev and we can not wait and get the return status. Hence we
  // read the whole output of the previewer and let it exit on its own.
  int status;
  if (waitpid(pid, &status, 0) == -1) {
    log_error("waitpid: %s", strerror(errno));
  } else {
    /* TODO: what other statuses are possible here? (on 2022-09-27) */
    if (WIFEXITED(status)) {
      const int ret = WEXITSTATUS(status);

      switch (ret) {
      case PREVIEW_DISPLAY_STDOUT:
        break;
      case PREVIEW_NONE:
        vec_str_clear(&p->lines);
        break; // no preview
      case PREVIEW_FILE_CONTENTS:
        vec_str_clear(&p->lines);
        FILE *fp_file = fopen(path, "r");
        if (fp_file) {
          for (uint32_t i = 0;
               i < height && fgets_seek(buf, sizeof buf, fp_file); i++) {
            vec_str_emplace(&p->lines, buf);
          }
          fclose(fp_file);
        } else {
          vec_str_emplace(&p->lines, strerror(errno));
          log_error("fopen: %s", strerror(errno));
        }
        break;
      case PREVIEW_FIX_WIDTH:
        p->reload_width = INT_MAX;
        break;
      case PREVIEW_FIX_HEIGHT:
        p->reload_height = INT_MAX;
        break;
      case PREVIEW_FIX_WIDTH_AND_HEIGHT:
        p->reload_width = INT_MAX;
        p->reload_height = INT_MAX;
        break;
      case PREVIEW_CACHE_AS_IMAGE:
        if (cfg.preview_images) {
          struct ncvisual *ncv = ncvisual_from_file(cache_path);
          if (ncv) {
            vec_str_drop(&p->lines);
            p->ncv = ncv;
            p->draw = draw_image_preview;
            p->update = update_image_preview;
            p->destroy = destroy_image_preview;
          } else {
            vec_str_clear(&p->lines);
            vec_str_emplace(&p->lines, "error loading image preview");
            log_error("ncvisual_from_file %s", cache_path);
          }
        }
        break;
      case PREVIEW_AS_IMAGE:
        if (cfg.preview_images) {
          struct ncvisual *ncv = ncvisual_from_file(path);
          if (ncv) {
            vec_str_drop(&p->lines);
            p->ncv = ncv;
            p->draw = draw_image_preview;
            p->update = update_image_preview;
            p->destroy = destroy_image_preview;
          } else {
            vec_str_clear(&p->lines);
            vec_str_emplace(&p->lines, "error (ncvisual_from_file)");
            log_error("ncvisual_from_file %s", path);
          }
        }
        break;
      default:
        log_error("unexpected return code %d from previewer for file %s", ret,
                  path);
      }
    }
  }

  fclose(fp);
  return p;
}

static void draw_text_preview(const Preview *p, struct ncplane *n) {
  ncplane_erase(n);

  unsigned int nrow;
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);

  for (int i = 0; i < vec_str_size(&p->lines) && i < (int)nrow; i++) {
    ncplane_cursor_move_yx(n, i, 0);
    ncplane_set_fg_default(n);
    ncplane_set_bg_default(n);
    ncplane_set_styles(n, NCSTYLE_NONE);
    ncplane_addastr(n, *vec_str_at(&p->lines, i));
  }
}

static void draw_image_preview(const Preview *p, struct ncplane *n) {
  ncplane_erase(n);

  if (!p->ncv) {
    return;
  }
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
  xfree(p->path);
  xfree(p);
}
