#pragma once

#include <stdbool.h>

struct lfm_s;

bool find(struct lfm_s *lfm, const char *prefix);

void find_clear(struct lfm_s *lfm);

void find_next(struct lfm_s *lfm);

void find_prev(struct lfm_s *lfm);
