#pragma once

#include "defs.h"
#include "fm.h"

// Begin visual selection mode.
void visual_enter_mode(Fm *fm);

// End visual selection mode.
void visual_exit_mode(Fm *fm);

// Update selection after the cursor moves in the current directory.
void visual_update_selection(Fm *fm, u32 from, u32 to);
