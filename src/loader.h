#pragma once

#include <ev.h>

#include "dir.h"
#include "hashtab.h"
#include "preview.h"

typedef struct loader_s {
  struct lfm_s *lfm;

  Hashtab *dir_cache;
  Hashtab *preview_cache;
  uint32_t dir_cache_version;  // number of times the cache has been dropped
  uint32_t preview_cache_version;

  ev_timer **dir_timers;
  ev_timer **preview_timers;
} Loader;

void loader_init(Loader *loader, struct lfm_s *lfm);
void loader_deinit(Loader *loader);
// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule(Loader *loader);

Dir *loader_dir_from_path(Loader *loader, const char *path);
void loader_dir_reload(Loader *loader, Dir *dir);
Hashtab *loader_dir_hashtab(Loader *loader);
void loader_drop_dir_cache(Loader *loader);

Preview *loader_preview_from_path(Loader *loader, const char *path, bool image);
void loader_preview_reload(Loader *loader, Preview *pv);
Hashtab *loader_pv_hashtab(Loader *loader);
void loader_drop_preview_cache(Loader *loader);
