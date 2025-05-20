#pragma once

#include "containers.h"
#include "macros_defs.h"
#include "stc/cstr.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

struct ncplane;
struct Preview;

typedef enum {
  PV_LOADING_DELAYED = 0,
  PV_LOADING_INITIAL,
  PV_LOADING_NORMAL
} pv_loading_status;

typedef void (*preview_draw_fun)(const struct Preview *, struct ncplane *);
typedef void (*preview_update_fun)(struct Preview *, struct Preview *);
typedef void (*preview_destroy_fun)(struct Preview *);

typedef struct Preview {
  cstr path;
  union {
    vec_cstr lines;
    struct ncvisual *ncv;
  };
  uint64_t next;
  time_t mtime;
  uint64_t loadtime;
  bool loading;
  pv_loading_status status;
  preview_draw_fun draw;
  preview_update_fun update;
  preview_destroy_fun destroy;
  int reload_width;  // geometry of the preview window when this preview was
                     // loaded
  int reload_height; // checked to see if a reload is necessary, INT_MAX when
                     // disabled.
} Preview;

__lfm_nonnull()
Preview *preview_create_loading(const char *path, int height, int width);

__lfm_nonnull()
Preview *preview_create_from_file(const char *path, uint32_t width,
                                  uint32_t height);

__lfm_nonnull()
static inline const cstr *preview_path(const Preview *pv) {
  return &pv->path;
}

__lfm_nonnull()
static inline const char *preview_path_str(const Preview *pv) {
  return cstr_str(&pv->path);
}

__lfm_nonnull()
static inline void preview_update(Preview *pv, Preview *u) {
  pv->update(pv, u);
}

__lfm_nonnull(1, 2)
static inline void preview_draw(const Preview *pv, struct ncplane *n) {
  pv->draw(pv, n);
}

static inline void preview_destroy(Preview *pv) {
  if (pv) {
    pv->destroy(pv);
  }
}
