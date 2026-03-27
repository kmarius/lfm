#pragma once

#include "defs.h"
#include "fm.h"
#include "mode.h"

extern struct mode visual_mode;

// Update selection after the cursor moves in the current directory.
void visual_update_selection(Fm *fm, u32 from, u32 to);
