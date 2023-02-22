#pragma once

#include <ev.h>

#include "dir.h"
#include "hashtab.h"
#include "preview.h"

typedef struct loader_s {
  struct lfm_s *lfm;

  Hashtab *dir_cache;
  Hashtab *preview_cache;
  size_t dir_cache_version;  // number of times the cache has been dropped
  size_t preview_cache_version;

  ev_timer **dir_timers;
  ev_timer **preview_timers;
} Loader;

void loader_init(Loader *loader, struct lfm_s *lfm);
void loader_deinit(Loader *loader);
// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule(Loader *loader);

Dir *loader_dir_from_path(Loader *loader, const char *path);
void loader_dir_reload(Loader *loader, Dir *dir);
// Schould be called when the load completes, i.e. the update is applied to
// the directory.
void loader_dir_load_callback(Loader *loader, Dir *dir);
void loader_drop_dir_cache(Loader *loader);

Preview *loader_preview_from_path(Loader *loader, const char *path);
void loader_preview_reload(Loader *loader, Preview *pv);
void loader_drop_preview_cache(Loader *loader);
