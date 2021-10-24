#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stdlib.h>

/* TODO: put this in the config (on 2021-07-29) */
#define NOTIFY_TIMEOUT 250 /* ms */
#define NOTIFY_DELAY 25	/* ms */

extern int inotify_fd;

bool notify_init();

void notify_add_watcher(const char *path);

void notify_remove_watcher(const char *path);

void notify_set_watchers(const char *const *paths, int n);

const char *notify_get_path(int wd);

void notify_close();

void log_watchers();

#endif /* NOTIFY_H */
