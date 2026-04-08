#pragma once

#include "dir.h"
#include "preview.h"

#include <stc/cstr.h>

struct load_timer;
struct Preview;
struct Dir;

#include <stc/types.h>
declare_dlist(list_load_timer, struct load_timer);
declare_hmap(map_zsview_preview, zsview, struct Preview *);

// key is zsview of dir->path and owned by dir
#define i_type map_zsview_dir
#define i_key zsview
#define i_val Dir *
#define i_valdrop(p) dir_dec_ref(*(p))
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_no_clone
#include <stc/hmap.h>

struct loader_ctx {
  map_zsview_dir dir_cache;
  list_load_timer dir_timers;
  map_zsview_preview preview_cache;
  list_load_timer preview_timers;
};

void loader_ctx_init(struct loader_ctx *loader);
void loader_ctx_deinit(struct loader_ctx *loader);

// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule(struct loader_ctx *loader);

//
// Directories
//

Dir *loader_dir_from_path(struct loader_ctx *loader, zsview path, bool do_load);
void loader_dir_reload(struct loader_ctx *loader, Dir *dir);
// Schould be called when the load completes, i.e. the update is applied to
// the directory.
void loader_dir_load_callback(struct loader_ctx *loader, Dir *dir);
void loader_drop_dir_cache(struct loader_ctx *loader);

//
// Previews
//

Preview *loader_preview_from_path(struct loader_ctx *loader, zsview path,
                                  bool do_load);
Preview *loader_preview_get(struct loader_ctx *loader, zsview path);
void loader_preview_reload(struct loader_ctx *loader, Preview *pv);
void loader_drop_preview_cache(struct loader_ctx *loader);
