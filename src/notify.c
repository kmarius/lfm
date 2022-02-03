#include <errno.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "config.h"
#include "cvector.h"
#include "log.h"
#include "notify.h"
#include "util.h"

#define NOTIFY_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

int inotify_fd = -1;

#define unwatch(t) \
	do { \
		inotify_rm_watch(inotify_fd, (t).wd); \
	} while (0)

static cvector_vector_type(struct watcher_data) watchers = NULL;

bool notify_init()
{
	inotify_fd = inotify_init1(IN_NONBLOCK);
	return inotify_fd != -1;
}

void notify_add_watcher(Dir *dir)
{
	if (inotify_fd == -1)
		return;

	for (size_t i = 0; i < cvector_size(cfg.inotify_blacklist); i++) {
		if (hasprefix(dir->path, cfg.inotify_blacklist[i]))
			return;
	}

	for (size_t i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].dir == dir)
			return;
	}

	const uint64_t t0 = current_millis();
	int wd = inotify_add_watch(inotify_fd, dir->path, NOTIFY_EVENTS);
	if (wd == -1) {
		log_error("inotify: %s", strerror(errno));
		return;
	}
	const uint64_t t1 = current_millis();

	/* TODO: inotify_add_watch can take over 200ms for example on samba shares.
	 * the only way to work around it is to add notify watches asnc. (on 2021-11-15) */
	if (t1 - t0 > 10)
		log_warn("inotify_add_watch(fd, \"%s\", ...) took %ums", dir->path, t1 - t0);

	cvector_push_back(watchers, ((struct watcher_data) {wd, dir, 0}));
}

void notify_remove_watcher(Dir *dir)
{
	if (inotify_fd == -1)
		return;

	for (size_t i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].dir == dir) {
			cvector_swap_ferase(watchers, unwatch, i);
			return;
		}
	}
}

void notify_set_watchers(Dir **dirs, uint16_t n)
{
	if (inotify_fd == -1)
		return;

	cvector_fclear(watchers, unwatch);

	for (uint16_t i = 0; i < n; i++) {
		if (dirs[i])
			notify_add_watcher(dirs[i]);
	}
}

struct watcher_data *notify_get_watcher_data(int wd)
{
	if (inotify_fd == -1)
		return NULL;

	for (size_t i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].wd == wd)
			return &watchers[i];
	}
	return NULL;
}

void notify_close()
{
	cvector_ffree(watchers, unwatch);
	close(inotify_fd);
	inotify_fd = -1;
}
