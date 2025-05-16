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

#define MAX_LINE_LENGTH 1024 // includes escapes and color codes

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

// Like fgets, but seeks to the next '\n' after n characters have been read
// Returns -1 on EOF
static inline ssize_t fgets_seek(char *dest, int n, FILE *fp) {
  int c = 0;
  char *ptr = dest;

  while (--n > 0 && (c = getc(fp)) != EOF) {
    if ((*ptr++ = c) == '\n') {
      break;
    }
  }

  if (c != EOF && c != '\n') {
    while ((c = getc(fp)) != EOF && c != '\n')
      ;
  }

  *ptr = 0;
  return (c == EOF && ptr == dest) ? -1 : ptr - dest;
}

// Read up to max_lines lines from a stream into a vector. Stops reading a line
// after MAX_LINE_LENGTH bytes.
static inline void lines_from_stream(vec_str *vec, FILE *file, int max_lines) {
  char buf[MAX_LINE_LENGTH];
  for (int i = 0; i < max_lines; i++) {
    ssize_t len = fgets_seek(buf, sizeof buf, file);
    if (len < 0) {
      return;
    }
    vec_str_push_back(vec, strndup(buf, len));
  }
}

// caller must should probably just pass a buffer of size PATH_MAX
static inline int gen_cache_path(const char *path, char *buf, size_t buflen) {
  uint8_t hash[32];
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (uint8_t *)path, strlen(path));
  sha256_final(&ctx, hash);
  unsigned int j = snprintf(buf, buflen - 1, "%s/", cfg.cachedir);
  if (j + 32 >= buflen) {
    return -1;
  }
  for (size_t i = 0; i < 32; i++) {
    const char upper = hash[i] >> 4;
    const char lower = hash[i] & 0x0f;
    buf[j++] = lower < 10 ? '0' + lower : 'a' + lower - 10;
    buf[j++] = upper < 10 ? '0' + upper : 'a' + upper - 10;
  }
  buf[j] = 0;
  return 0;
}

Preview *preview_create_from_file(const char *path, uint32_t width,
                                  uint32_t height) {

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
    if (gen_cache_path(path, cache_path, sizeof cache_path) != 0) {
      log_error("gen_cache_path");
      vec_str_emplace(&p->lines, "gen_cache_path");
      return p;
    };
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

  FILE *fp_stdout = NULL;

  int pid = popen2_arr_p(NULL, &fp_stdout, NULL, args[0], (char *const *)args,
                         NULL, NULL);

  if (pid == -1 || fp_stdout == NULL) {
    if (fp_stdout != NULL) {
      fclose(fp_stdout);
    }
    vec_str_emplace(&p->lines, strerror(errno));
    log_error("popen2_arr_p: %s", strerror(errno));
    return p;
  }

  // waitpid immediately, otherwise libev might reap the child before us

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    char buf[128];
    snprintf(buf, sizeof buf - 1, "waitpid: %s", strerror(errno));
    vec_str_emplace(&p->lines, buf);
    log_error("waitpid: %s", strerror(errno));
  } else {
    if (WIFEXITED(status)) {
      int rc = WEXITSTATUS(status);

      switch (rc) {
      case PREVIEW_DISPLAY_STDOUT:
        lines_from_stream(&p->lines, fp_stdout, height);
        break;
      case PREVIEW_NONE:
        break; // no preview
      case PREVIEW_FILE_CONTENTS:
        FILE *fp_file = fopen(path, "r");
        if (fp_file == NULL) {
          vec_str_emplace(&p->lines, strerror(errno));
          log_error("fopen: %s", strerror(errno));
        } else {
          lines_from_stream(&p->lines, fp_file, height);
          fclose(fp_file);
        }
        break;
      case PREVIEW_FIX_WIDTH:
        p->reload_width = INT_MAX;
        lines_from_stream(&p->lines, fp_stdout, height);
        break;
      case PREVIEW_FIX_HEIGHT:
        p->reload_height = INT_MAX;
        lines_from_stream(&p->lines, fp_stdout, height);
        break;
      case PREVIEW_FIX_WIDTH_AND_HEIGHT:
        p->reload_width = INT_MAX;
        p->reload_height = INT_MAX;
        lines_from_stream(&p->lines, fp_stdout, height);
        break;
      case PREVIEW_CACHE_AS_IMAGE:
        if (cfg.preview_images) {
          struct ncvisual *ncv = ncvisual_from_file(cache_path);
          if (ncv == NULL) {
            vec_str_emplace(&p->lines, "error (ncvisual_from_file)");
            log_error("ncvisual_from_file %s", cache_path);
          } else {
            vec_str_drop(&p->lines);
            p->ncv = ncv;
            p->draw = draw_image_preview;
            p->update = update_image_preview;
            p->destroy = destroy_image_preview;
          }
        }
        break;
      case PREVIEW_AS_IMAGE:
        if (cfg.preview_images) {
          struct ncvisual *ncv = ncvisual_from_file(path);
          if (ncv == NULL) {
            vec_str_emplace(&p->lines, "error (ncvisual_from_file)");
            log_error("ncvisual_from_file %s", path);
          } else {
            vec_str_drop(&p->lines);
            p->ncv = ncv;
            p->draw = draw_image_preview;
            p->update = update_image_preview;
            p->destroy = destroy_image_preview;
          }
        }
        break;
      default:
        log_error("unexpected return code %d from previewer for file %s", rc,
                  path);
      }
    }
  }

  fclose(fp_stdout);
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
