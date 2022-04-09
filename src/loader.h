#pragma once

#include "app.h"
#include "dir.h"

void loader_init(App *app);
void loader_deinit();
void loader_load(Dir *dir);

// That queue holds references to directories that are invalidated on drop_cache.
void loader_empty_queue();

// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule();
