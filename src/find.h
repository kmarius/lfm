#pragma once

#include <stdbool.h>

#include "fm.h"
#include "ui.h"

bool find(Fm *fm, Ui *ui, const char *prefix);

void find_clear();

void find_next(Fm *fm, Ui *ui);

void find_prev(Fm *fm, Ui *ui);
