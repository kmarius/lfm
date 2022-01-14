#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "util.h"

#define NOTIFY_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

typedef struct tup_t {
	Dir *dir;
	int wd;
} tup_t;

#define unwatch(t) \
	do { \
		inotify_rm_watch(inotify_fd, (t).wd); \
	} while (0)

int inotify_fd = -1;

static cvector_vector_type(tup_t) watchers = NULL;

bool notify_init()
{
	inotify_fd = inotify_init1(IN_NONBLOCK);
	return inotify_fd != -1;
}

void notify_add_watcher(Dir *dir)
{
	int wd;
	size_t i;

	if (inotify_fd == -1) {
		return;
	}
	for (i = 0; i < cvector_size(cfg.inotify_blacklist); i++) {
		if (hasprefix(dir->path, cfg.inotify_blacklist[i])) {
			return;
		}
	}

	for (i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].dir == dir) {
			return;
		}
	}

	const unsigned long t0 = current_millis();
	if ((wd = inotify_add_watch(inotify_fd, dir->path, NOTIFY_EVENTS)) == -1) {
		log_error("inotify: %s", strerror(errno));
		return;
	}
	const unsigned long t1 = current_millis();

	/* TODO: inotify_add_watch can take over 200ms for example on samba shares.
	 * the only way to work around it is to add notify watches asnc. (on 2021-11-15) */
	if (t1-t0 > 10) {
		log_warn("inotify_add_watch(fd, \"%s\", ...) took %ums", dir->path, t1 - t0);
	}

	cvector_push_back(watchers, ((tup_t) {dir, wd}));
}

void notify_remove_watcher(Dir *dir)
{
	size_t i;

	if (inotify_fd == -1) {
		return;
	}

	for (i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].dir == dir) {
			cvector_swap_ferase(watchers, unwatch, (unsigned int) i);
			return;
		}
	}
}

void notify_set_watchers(Dir **dirs, int n)
{
	int i;

	if (inotify_fd == -1) {
		return;
	}

	cvector_fclear(watchers, unwatch);

	for (i = 0; i < n; i++) {
		if (dirs[i] != NULL) {
			notify_add_watcher(dirs[i]);
		}
	}
}

void log_watchers()
{
	size_t i;

	for (i = 0; i < cvector_size(watchers); i++) {
		log_debug("watchers: %s", watchers[i].dir->path);
	}
}

Dir *notify_get_dir(int wd)
{
	size_t i;

	if (inotify_fd == -1) {
		return NULL;
	}

	for (i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].wd == wd) {
			return watchers[i].dir;
		}
	}
	return NULL;
}

void notify_close()
{
	cvector_ffree(watchers, unwatch);
	close(inotify_fd);
	inotify_fd = -1;
}
