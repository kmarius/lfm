#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stdlib.h>

#include "dir.h"

/* TODO: put this in the config (on 2021-07-29) */
#define NOTIFY_TIMEOUT 250 /* ms */
#define NOTIFY_DELAY 25	/* ms */

extern int inotify_fd;

bool notify_init();

void notify_add_watcher(dir_t *dir);

void notify_remove_watcher(dir_t *dir);

void notify_set_watchers(dir_t **dirs, int n);

dir_t *notify_get_dir(int wd);

void notify_close();

void log_watchers();

#endif /* NOTIFY_H */
