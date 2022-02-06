#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dir.h"

/* TODO: put this in the config (on 2021-07-29) */
#define NOTIFY_TIMEOUT 500 // minimum time between directory reloads
#define NOTIFY_DELAY 50	// delay before reloading after an event is triggered

struct notify_watcher_data {
	int wd;
	Dir *dir;
	uint64_t next;
};

// Returns a file descriptor to watch for events or -1 on failure.
int notify_init();

void notify_add_watcher(Dir *dir);

void notify_remove_watcher(Dir *dir);

void notify_set_watchers(Dir **dirs, uint16_t n);

struct notify_watcher_data *notify_get_watcher_data(int wd);

void notify_deinit();
