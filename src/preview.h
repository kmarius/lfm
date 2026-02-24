#pragma once

#include "bytes.h"
#include "macros.h"
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
  uint32_t width; // geometry of the preview window when this preview was loaded
                  // requested width and height of this preview checked to see
                  // if a reload is necessary, INT_MAX when disabled.
  uint32_t height;
  union {
    bytes data;
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
} Preview;

__lfm_nonnull()
Preview *preview_error(Preview *p, const char *fmt, ...);

__lfm_nonnull()
Preview *preview_create_loading(zsview path, int height, int width);

__lfm_nonnull()
Preview *preview_create_and_stat(zsview path, int height, int width);

__lfm_nonnull()
Preview *preview_fork_previewer(zsview path, uint32_t width, uint32_t height,
                                int *pid_out, int fd_out[2]);

__lfm_nonnull()
Preview *preview_read_output(Preview *p, int fd[2]);

__lfm_nonnull()
Preview *preview_handle_exit_status(Preview *p, int status);

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
