#include "profiling.h"

struct profiling_data profiling_data = {0};

struct profiling_data *get_profiling_data() {
  return &profiling_data;
}
