#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cvector.h"

struct ncplane;
struct preview_s;

typedef void (*preview_draw_fun)(const struct preview_s *, struct ncplane *);
typedef void (*preview_update_fun)(struct preview_s *, struct preview_s *);
typedef void (*preview_destroy_fun)(struct preview_s *);

typedef struct preview_s {
  char *path;
  union {
    cvector_vector_type(char *) lines;
    struct ncvisual *ncv;
  };
  uint64_t next;
  time_t mtime;
  uint64_t loadtime;
  bool loading;
  preview_draw_fun draw;
  preview_update_fun update;
  preview_destroy_fun destroy;
  int reload_width;  // geometry of the preview window when this preview was
                     // loaded
  int reload_height; // checked to see if a reload is necessary, INT_MAX when
                     // disabled.
} Preview;

Preview *preview_create_loading(const char *path, int height, int width);

Preview *preview_create_from_file(const char *path, uint32_t width,
                                  uint32_t height);

static inline void preview_update(Preview *pv, Preview *u) {
  pv->update(pv, u);
}

static inline void preview_draw(const Preview *pv, struct ncplane *n) {
  if (!pv) {
    return;
  }
  pv->draw(pv, n);
}

static inline void preview_destroy(Preview *pv) {
  if (pv) {
    pv->destroy(pv);
  }
}
