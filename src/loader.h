#pragma once

#include "dir.h"      // must be included before dircache.h
#include "stc/cstr.h" // must be included before dircache.h

#include "dircache.h"
#include "preview.h"

#include "stc/types.h"

struct loader_timer;
declare_dlist(list_loader_timer, struct loader_timer);
declare_hmap(previewcache, cstr, Preview *);

typedef struct Loader {
  dircache dc;
  previewcache pc;
  size_t dir_cache_version; // number of times the cache has been dropped
  size_t preview_cache_version;

  list_loader_timer dir_timers;
  list_loader_timer preview_timers;
} Loader;

void loader_init(Loader *loader);
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
Preview *loader_preview_get(Loader *loader, const char *path);
void loader_preview_reload(Loader *loader, Preview *pv);
void loader_drop_preview_cache(Loader *loader);
