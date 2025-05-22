#pragma once

#include "util.h"

#include <stdint.h>

struct profiling_entry {
  uint64_t ts, diff;
};

struct profiling_data {
  uint64_t startup;
  struct profiling_entry lfm_init;
  struct profiling_entry fm_init;
  struct profiling_entry ui_init;
  struct profiling_entry lua_init;
  struct profiling_entry lua_core;
  struct profiling_entry user_config;
};

extern struct profiling_data profiling_data;
#define PROFILING_INIT()                                                       \
  do {                                                                         \
    profiling_data.startup = current_micros();                                 \
  } while (0)

#define PROFILE(field, BODY)                                                   \
  do {                                                                         \
    profiling_data.field.ts = current_micros();                                \
    BODY profiling_data.field.diff =                                           \
        current_micros() - profiling_data.field.ts;                            \
    profiling_data.field.ts -= profiling_data.startup;                         \
  } while (0);

// for ffi
struct profiling_data *get_profiling_data();
