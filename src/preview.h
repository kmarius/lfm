#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cvector.h"
#include "log.h"

struct ncplane;
struct Preview;

typedef void (*preview_draw_fun)(const struct Preview *, struct ncplane *);
typedef void (*preview_update_fun)(struct Preview *, struct Preview *);
typedef void (*preview_destroy_fun)(struct Preview *);

typedef struct Preview {
  char *path;
  union {
    cvector_vector_type(char *) lines;
    struct ncvisual *ncv;
  };
  uint32_t nrow;
  time_t mtime;
  uint64_t loadtime;
  bool loading;
  preview_draw_fun draw;
  preview_update_fun update;
  preview_destroy_fun destroy;
} Preview;

Preview *preview_create_loading(const char *path, uint32_t nrow);

Preview *preview_create_from_file(const char *path, uint32_t nrow);

static inline void preview_update(Preview *pv, Preview *u)
{
  pv->update(pv, u);
}

static inline void preview_draw(const Preview *pv, struct ncplane *n)
{
  if (!pv) {
    return;
  }
  pv->draw(pv, n);
}

static inline void preview_destroy(Preview *pv)
{
  pv->destroy(pv);
}
