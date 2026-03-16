#include "profiling.h"

struct profiling_data profiling_data = {0};

i32 profiling_depth = 0;
bool profiling_complete = false;

struct profiling_data *get_profiling_data() {
  return &profiling_data;
}
