#pragma once

#include <stdbool.h>

#include "fm.h"

bool find(Fm *fm, const char *prefix);

void find_clear(Fm *fm);

void find_next(Fm *fm);

void find_prev(Fm *fm);
