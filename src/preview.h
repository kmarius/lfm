#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cvector.h"

typedef struct Preview {
  char *path;
  cvector_vector_type(char*) lines;
  uint8_t nrow;
  time_t mtime;
  uint64_t loadtime;
  bool loading;
} Preview;

Preview *preview_create_loading(const char *path, uint8_t nrow);

Preview *preview_create_from_file(const char *path, uint8_t nrow);

void preview_update_with(Preview *pv, Preview *update);

void preview_destroy(Preview *pv);
