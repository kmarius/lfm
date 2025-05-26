#pragma once

#include "util.h"

#include <stdbool.h>
#include <stdint.h>

// is currently includes a handful of items from startup, and many "require"d
// lua modules from startup and runtime
#define PROFILING_MAX_ENTRIES 128

struct profiling_entry {
  uint64_t ts, diff;
  const char *name;
  int depth;
  bool is_complete;
};

struct profiling_data {
  uint64_t startup;
  int num_entries;
  struct profiling_entry entries[PROFILING_MAX_ENTRIES];
};

extern struct profiling_data profiling_data;
extern int profiling_depth;
extern bool profiling_complete;

#define PROFILING_INIT()                                                       \
  do {                                                                         \
    profiling_data.num_entries = 0;                                            \
    profiling_data.startup = current_micros();                                 \
  } while (0)

#define PROFILING_COMPLETE()                                                   \
  do {                                                                         \
    profiling_complete = true;                                                 \
  } while (0)

#define PROFILE_MAYBE(name_, BODY)                                             \
  do {                                                                         \
    if (profiling_complete) {                                                  \
      BODY;                                                                    \
    } else {                                                                   \
      PROFILE((name_), BODY)                                                   \
    }                                                                          \
  } while (0);

#define PROFILE(name_, BODY)                                                   \
  do {                                                                         \
    if (profiling_data.num_entries == PROFILING_MAX_ENTRIES) {                 \
      BODY;                                                                    \
    } else {                                                                   \
      struct profiling_entry *entry =                                          \
          &profiling_data.entries[profiling_data.num_entries++];               \
      entry->depth = profiling_depth++;                                        \
      entry->ts = current_micros();                                            \
      do {                                                                     \
        BODY;                                                                  \
      } while (0);                                                             \
      entry->diff = current_micros() - entry->ts;                              \
      entry->ts -= profiling_data.startup;                                     \
      entry->name = (name_);                                                   \
      entry->is_complete = 1;                                                  \
      profiling_depth--;                                                       \
    }                                                                          \
  } while (0);

// for ffi
struct profiling_data *get_profiling_data();
