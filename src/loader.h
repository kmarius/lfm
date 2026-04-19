#pragma once

#include <stc/cstr.h>

struct load_timer;
struct Preview;
struct Dir;

#include <stc/types.h>
declare_hmap(map_loadable_timer, struct loadable_data *, struct load_timer *);
declare_hmap(map_zsview_preview, zsview, struct Preview *);

#include "dir.h"
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
  // maps loadables (directories, previews) to an internal struct holding a
  // timer and more (only for active timers)
  map_loadable_timer timers;

  // all live directories; there could be additional ones
  // that have been dropped (by loader_drop_dir_cache) but still have references
  // from lua or an async worker reloading it. Those will be destroyed once
  // those reerences drop.
  map_zsview_dir dir_cache;

  // same as dir_cache, but for previews
  map_zsview_preview preview_cache;
};

void loader_ctx_init(struct loader_ctx *loader);
void loader_ctx_deinit(struct loader_ctx *loader);

// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule(struct loader_ctx *loader);

// notify loader that a loadable has completed
void loader_callback(struct loader_ctx *ctx, struct loadable_data *loadable);

//
// Directories
//

Dir *loader_dir_from_path(struct loader_ctx *loader, zsview path, bool do_load);
// Reload the given directory. Takes care of throttling.
void loader_dir_reload(struct loader_ctx *loader, Dir *dir);
void loader_drop_dir_cache(struct loader_ctx *loader);

//
// Previews
//

struct Preview *loader_preview_from_path(struct loader_ctx *loader, zsview path,
                                         bool do_load);
// Reload the given preview. Takes care of throttling.
void loader_preview_reload(struct loader_ctx *loader, struct Preview *pv);
void loader_drop_preview_cache(struct loader_ctx *loader);
