#pragma once

#include <stdbool.h>
#include <stdlib.h>

#include "dir.h"

/* TODO: put this in the config (on 2021-07-29) */
#define NOTIFY_TIMEOUT 500 /* ms */
#define NOTIFY_DELAY 25	/* ms */

extern int inotify_fd;

bool notify_init();

void notify_add_watcher(Dir *dir);

void notify_remove_watcher(Dir *dir);

void notify_set_watchers(Dir **dirs, uint16_t n);

Dir *notify_get_dir(int wd);

void notify_close();
