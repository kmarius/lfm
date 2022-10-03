#include <errno.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>

#include "config.h"
#include "cvector.h"
#include "log.h"
#include "ncutil.h"
#include "popen_arr.h"
#include "preview.h"
#include "sha256.h"
#include "util.h"

#define PREVIEW_MAX_LINE_LENGTH 1024  // includes escapes and color codes

static void draw_text_preview(const Preview *p, struct ncplane *n);
static void update_text_preview(Preview *p, Preview *u);
static void destroy_text_preview(Preview *p);

static void draw_image_preview(const Preview *p, struct ncplane *n);
static void update_image_preview(Preview *p, Preview *u);
static void destroy_image_preview(Preview *p);

static inline Preview *preview_init(Preview *p, const char *path, uint32_t nrow)
{
  memset(p, 0, sizeof *p);
  p->path = strdup(path);
  p->nrow = nrow;
  p->next = current_millis();

  p->draw = draw_text_preview;
  p->update = update_text_preview;
  p->destroy = destroy_text_preview;

  return p;
}


static inline Preview *preview_create(const char *path, uint32_t nrow)
{
  return preview_init(malloc(sizeof(Preview)), path, nrow);
}


static void destroy_text_preview(Preview *p)
{
  if (!p) {
    return;
  }
  cvector_ffree(p->lines, free);
  free(p->path);
  free(p);
}


Preview *preview_create_loading(const char *path, uint32_t nrow)
{
  Preview *p = preview_create(path, nrow);
  p->loading = true;
  return p;
}


static void update_text_preview(Preview *p, Preview *u)
{
  log_debug("update_text_preview %s", p->path);
  cvector_ffree(p->lines, free);
  p->lines = u->lines;
  p->mtime = u->mtime;
  p->nrow = u->nrow;
  p->loadtime = u->loadtime;
  p->loading = false;

  p->draw = u->draw;
  p->update = u->update;
  p->destroy = u->destroy;

  free(u->path);
  free(u);
}


static void update_image_preview(Preview *p, Preview *u)
{
  log_debug("update_image_preview %s", p->path);
  if (p->ncv) {
    ncvisual_destroy(p->ncv);
  }
  p->ncv = u->ncv;
  p->mtime = u->mtime;
  p->loadtime = u->loadtime;
  p->loading = false;

  free(u->path);
  free(u);
}


// like fgets, but seeks to the next line if dest is full.
static inline char *fgets_seek(char* dest, int n, FILE *fp)
{
  int c;
  char* cs;
  cs = dest;

  while (--n > 0 && (c = getc(fp)) != EOF) {
    if ((*cs++ = c) == '\n') {
      break;
    }
  }

  if (c != EOF && c != '\n') {
    while ((c = getc(fp)) != EOF && c != '\n');
  }

  *cs = 0;
  return (c == EOF && cs == dest) ? NULL : dest;
}


static inline void gen_cache_path(char *cache_path, const char *path)
{
  uint8_t buf[32];
  SHA256_CTX ctx;
  sha256_init(&ctx);
  sha256_update(&ctx, (uint8_t *) path, strlen(path));
  sha256_final(&ctx, buf);
  cache_path += sprintf(cache_path, "%s/", cfg.cachedir);
  for (size_t i = 0; i < 32; i++) {
    const char upper = buf[i] >> 4;
    const char lower = buf[i] & 0x0f;
    cache_path[2*i] = lower < 10 ? '0' + lower : 'a' + lower - 10;
    cache_path[2*i+1] = upper < 10 ? '0' + upper : 'a' + upper - 10;
  }
  cache_path[2*32] = 0;
}


Preview *preview_create_from_file(const char *path, uint32_t width, uint32_t height)
{
  char buf[PREVIEW_MAX_LINE_LENGTH];

  Preview *p = preview_create(path, height);
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
    log_debug("generated: %s", cache_path);
  } else {
    cache_path[0] = 0;
  }

  char w[32];
  char h[32];
  snprintf(w, sizeof w, "%u", width);
  snprintf(h, sizeof h, "%u", height);

  char *const args[7] = {
    cfg.previewer,
    p->path,
    w, h,
    cache_path,
    cfg.preview_images ? "True" : "False",
    NULL};

  FILE *fp = NULL;
  int pid = popen2_arr_p(NULL, &fp, NULL, args[0], args, NULL);
  if (!fp) {
    cvector_push_back(p->lines, strerror(errno));
    log_error("preview: %s", strerror(errno));
    return p;
  }

  while (height-- > 0 && fgets_seek(buf, sizeof buf, fp)) {
    cvector_push_back(p->lines, strdup(buf));
  }
  while (getc(fp) != EOF) {}

  // if we try to close the pipe (so that the child exits), the process gets reaped
  // by libev and we can not wait and get the return status. Hence we read the
  // whole output of the previewer and let it exit on its own.
  int status;
  if (waitpid(pid, &status, 0) == -1) {
    log_error("waitpid failed");
  } else {
    /* TODO: what other statuses are possible here? (on 2022-09-27) */
    if (WIFEXITED(status)) {
      const int ret = WEXITSTATUS(status);
      // return code interpretation taken from ranger:
      switch (ret) {
        case 0: break; // display stdout
        case 1: cvector_fclear(p->lines, free); break; // no preview
       // case 2: // show file content
       // case 3: // fix width
       // case 4: // fix height
       // case 5: // fix both
        case 6: if (cfg.preview_images) { // load from cache path passed to the previewer
                  struct ncvisual *ncv = ncvisual_from_file(cache_path);
                  if (ncv) {
                    cvector_ffree(p->lines, free);
                    p->ncv = ncv;
                    p->draw = draw_image_preview;
                    p->update = update_image_preview;
                    p->destroy = destroy_image_preview;
                  } else {
                    cvector_fclear(p->lines, free);
                    cvector_push_back(p->lines, strdup("error loading image preview"));
                    log_error("error loading image preview from %s", cache_path);
                  }
                }
                break;
        case 7: if (cfg.preview_images) { // load file as image
                                          // try to load image preview
                  struct ncvisual *ncv = ncvisual_from_file(path);
                  if (ncv) {
                    cvector_ffree(p->lines, free);
                    p->ncv = ncv;
                    p->draw = draw_image_preview;
                    p->update = update_image_preview;
                    p->destroy = destroy_image_preview;
                  } else {
                    cvector_fclear(p->lines, free);
                    cvector_push_back(p->lines, strdup("error (ncvisual_from_file)"));
                    log_error("error loading image preview");
                  }
                }
                break;
        default:
                log_error("unexpected return code %d from previewer for file %s", ret, path);
      }
    }
  }

  fclose(fp);
  return p;
}


static void draw_text_preview(const Preview *p, struct ncplane *n)
{
  ncplane_erase(n);

  unsigned int nrow;
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);

  for (size_t i = 0; i < cvector_size(p->lines) && i < (size_t) nrow; i++) {
    ncplane_cursor_move_yx(n, i, 0);
    ncplane_set_fg_default(n);
    ncplane_set_bg_default(n);
    ncplane_set_styles(n, NCSTYLE_NONE);
    ansi_addstr(n, p->lines[i]);
  }
}


static void draw_image_preview(const Preview *p, struct ncplane *n)
{
  ncplane_erase(n);

  if (!p->ncv) {
    return;
  }
  struct ncvisual_options vopts = {
    .scaling = NCSCALE_SCALE,
    .n = n,
    .blitter = NCBLIT_PIXEL,
  };
  if (ncvisual_blit(ncplane_notcurses(n), p->ncv, &vopts) == NULL){
    log_error("ncvisual_blit error");
  }
}


static void destroy_image_preview(Preview *p)
{
  if (!p) {
    return;
  }
  if (p->ncv) {
    ncvisual_destroy(p->ncv);
    p->ncv = NULL;
  }
  free(p->path);
  free(p);
}
