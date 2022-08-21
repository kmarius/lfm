#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "cvector.h"
#include "log.h"
#include "ncutil.h"
#include "popen_arr.h"
#include "preview.h"
#include "util.h"

#define T Preview

#define PREVIEW_MAX_LINE_LENGTH 1024 // includes escapes and color codes
                                     //
static void draw_text_preview(const T *t, struct ncplane *n);
static void update_text_preview(T *t, Preview *u);
static void destroy_text_preview(Preview *t);

static void draw_image_preview(const T *t, struct ncplane *n);
static void update_image_preview(T *t, Preview *u);
static void destroy_image_preview(Preview *t);

static inline bool is_image(const char *path)
{
  /* TODO: disabled for now, until we can upgrade notcurses (on 2022-08-10) */
  // return false;
  return strcasestr(path, ".png") || strcasestr(path, ".jpg");
}


static inline T *preview_init(T *t, const char *path, uint32_t nrow)
{
  memset(t, 0, sizeof *t);
  t->path = strdup(path);
  t->nrow = nrow;
  if (is_image(path)) {
    t->draw = draw_image_preview;
    t->update = update_image_preview;
    t->destroy = destroy_image_preview;
  } else {
    t->draw = draw_text_preview;
    t->update = update_text_preview;
    t->destroy = destroy_text_preview;
  }
  return t;
}


static inline T *preview_create(const char *path, uint32_t nrow)
{
  return preview_init(malloc(sizeof(T)), path, nrow);
}


static void destroy_text_preview(Preview *t)
{
  if (!t) {
    return;
  }
  cvector_ffree(t->lines, free);
  free(t->path);
  free(t);
}


T *preview_create_loading(const char *path, uint32_t nrow)
{
  T *t = preview_create(path, nrow);
  t->loading = true;
  return t;
}


static void update_text_preview(T *t, Preview *u)
{
  log_debug("update_text_preview %s", t->path);
  cvector_ffree(t->lines, free);
  t->lines = u->lines;
  t->mtime = u->mtime;
  t->loadtime = u->loadtime;
  t->loading = false;

  free(u->path);
  free(u);
}


static void update_image_preview(T *t, Preview *u)
{
  log_debug("update_image_preview %s", t->path);
  if (t->ncv) {
    ncvisual_destroy(t->ncv);
  }
  t->ncv = u->ncv;
  t->mtime = u->mtime;
  t->loadtime = u->loadtime;
  t->loading = false;

  free(u->path);
  free(u);
}


// like fgets, but seeks to the next line if dest is full.
static char* fgets_seek(char* dest, int n, FILE *fp)
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


T *preview_create_from_file(const char *path, uint32_t nrow)
{
  char buf[PREVIEW_MAX_LINE_LENGTH];

  T *t = preview_create(path, nrow);
  t->loadtime = current_millis();

  struct stat statbuf;
  t->mtime = stat(path, &statbuf) != -1 ? statbuf.st_mtime : 0;

  if (!cfg.previewer) {
    return t;
  }

  if (is_image(path)) {
    t->ncv = ncvisual_from_file(path);
    log_debug("created image preview for %s %p", path, t->ncv);
  } else {
    /* TODO: redirect stderr? (on 2021-08-10) */
    // TODO:  (on 2022-08-21)
    // we can not reliably get the return status of the child here because
    // it might get reaped by ev in the main thread.
    // Once we can do that, we should e.g. adhere to ranger and print the preview
    // if the previewer exits with 7
    char *const args[3] = {cfg.previewer, (char*) path, NULL};
    FILE *fp = popen_arr(cfg.previewer, args, false);
    if (!fp) {
      log_error("preview: %s", strerror(errno));
      return t;
    }
    while (nrow-- > 0 && fgets_seek(buf, sizeof buf, fp)) {
      cvector_push_back(t->lines, strdup(buf));
    }
    pclose(fp);
  }
  return t;
}


static void draw_text_preview(const T *t, struct ncplane *n)
{
  ncplane_erase(n);

  unsigned int nrow;
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);

  for (size_t i = 0; i < cvector_size(t->lines) && i < (size_t) nrow; i++) {
    ncplane_cursor_move_yx(n, i, 0);
    ncplane_set_fg_default(n);
    ncplane_set_bg_default(n);
    ncplane_set_styles(n, NCSTYLE_NONE);
    ansi_addstr(n, t->lines[i]);
  }
}


static void draw_image_preview(const T *t, struct ncplane *n)
{
  ncplane_erase(n);

  if (!t->ncv) {
    return;
  }
  log_debug("drawing image preview %s", t->path);
  struct ncvisual_options vopts = {
    .scaling = NCSCALE_SCALE,
    .n = n,
    .blitter = NCBLIT_PIXEL,
  };
  if (ncvisual_blit(ncplane_notcurses(n), t->ncv, &vopts) == NULL){
    log_error("ncvisual_blit error");
  }
}


static void destroy_image_preview(Preview *t)
{
  if (!t) {
    return;
  }
  if (t->ncv) {
    ncvisual_destroy(t->ncv);
    t->ncv = NULL;
  }
  free(t->path);
  free(t);
}
