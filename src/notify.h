#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "dir.h"

/* TODO: put this in the config (on 2021-07-29) */
#define NOTIFY_TIMEOUT 500 // minimum time between directory reloads
#define NOTIFY_DELAY 50	// delay before reloading after an event is triggered

// Returns a file descriptor or -1 on failure.
int notify_init(App *app);

void notify_add_watcher(Dir *dir);

void notify_remove_watcher(Dir *dir);

void notify_set_watchers(Dir **dirs, uint16_t n);

// That queue holds references to directories that are invalidated on drop_cache.
void notify_empty_queue();

void notify_deinit();
