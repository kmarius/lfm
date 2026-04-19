#pragma once

#include "defs.h"

#include <stdbool.h>

struct loader_ctx;

// data embedded in preview and dir structs, used by loader
struct loadable_data {
  void (*load)(struct loader_ctx *loader,
               struct loadable_data *); // set in loader.c
  u64 next_scheduled; // time of the next (or latest) scheduled reload
  u64 next_requested; // set if a reaload is requested while one is already
                      // in progress
  bool is_scheduled;  // is a reload scheduled, set when scheduled, unset in cb
                      // (before schedule)
  bool in_progress;   // load is in progress (not just scheduled), set after
                      // calling load, unset in loader_callback
  bool is_disowned;   // set after dropping dir/preview cache to mark as invalid
};
