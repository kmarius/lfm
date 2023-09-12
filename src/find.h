#pragma once

#include <stdbool.h>

#include "lfm.h"

bool find(Lfm *lfm, const char *prefix);

void find_clear(Lfm *lfm);

void find_next(Lfm *lfm);

void find_prev(Lfm *lfm);
