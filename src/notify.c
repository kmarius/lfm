#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "cvector.h"
#include "log.h"
#include "util.h"

#define NOTIFY_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO )

typedef struct tup {
	char *path;
	int wd;
} tup_t;

#define free_tup(t) \
	do { \
		inotify_rm_watch(inotify_fd, (t).wd); \
		free((t).path); \
	} while (0)

int inotify_fd = -1;

static cvector_vector_type(tup_t) watchers = NULL;

bool notify_init()
{
	log_debug("notify_init");

	inotify_fd = inotify_init1(IN_NONBLOCK);

	if (inotify_fd == -1) {
		log_error("inotify: %s", strerror(errno));
		return false;
	}

	return true;
}

void notify_add_watcher(const char *path)
{
	/* log_debug("notify_add_watch %s", path); */

	int i, wd;

	if (inotify_fd == -1) {
		return;
	}

	const int l = cvector_size(watchers);
	for (i = 0; i < l; i++) {
		if (streq(path, watchers[i].path)) {
			return;
		}
	}

	if ((wd = inotify_add_watch(inotify_fd, path, NOTIFY_EVENTS)) == -1) {
		log_error("inotify: %s", strerror(errno));
		return;
	}

	tup_t t = {.path = strdup(path), .wd = wd};
	cvector_push_back(watchers, t);
}

void notify_remove_watcher(const char *path)
{
	/* log_debug("notify_remove_watch %s", path); */

	if (inotify_fd == -1) {
		return;
	}

	const int l = cvector_size(watchers);
	int i;
	for (i = 0; i < l; i++) {
		if (streq(watchers[i].path, path)) {
			cvector_ferase(watchers, free_tup, (unsigned int) i);
			return;
		}
	}
}

void notify_set_watchers(const char *const *paths, int n)
{
	int i, j;
	bool c;

	if (inotify_fd == -1) {
		return;
	}

	const int l = cvector_size(watchers);
	for (i = 0; i < l; i++) {
		c = false;
		for (j = 0; j < n; j++) {
			if (!paths[j]) {
				continue;
			}
			if ((c = streq(watchers[i].path, paths[j]))) {
				break;
			}
		}
		if (!c) {
			cvector_ferase(watchers, free_tup, (unsigned int) i);
		}
	}

	for (j = 0; j < n; j++) {
		if (paths[j]) {
			notify_add_watcher(paths[j]);
		}
	}

}

void log_watchers()
{
	const int m = cvector_size(watchers);
	int i;
	for (i = 0; i < m; i++) {
		log_debug("watchers: %s", watchers[i].path);
	}
}

const char *notify_get_path(int wd)
{
	int i;

	if (inotify_fd == -1) {
		return NULL;
	}

	const int l = cvector_size(watchers);
	for (i = 0; i < l; i++) {
		if (watchers[i].wd == wd) {
			return watchers[i].path;
		}
	}
	return NULL;
}

void notify_close()
{
	cvector_ffree(watchers, free_tup);
	close(inotify_fd);
	inotify_fd = -1;
}
