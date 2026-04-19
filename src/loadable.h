#pragma once

#include "defs.h"

#include <stdbool.h>

// data embedded in preview and dir structs, used by loader
struct loadable_data {
  u64 next_scheduled; // time of the next (or latest) scheduled reload
  u64 next_requested; // will be set if a reload is requested when
                      // one is already scheduled, otherwise 0
  bool is_scheduled;  // is a reload scheduled
};
