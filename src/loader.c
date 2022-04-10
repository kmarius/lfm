#include <ev.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "hashtab.h"
#include "loader.h"
#include "util.h"

static struct ev_loop *loop = NULL;
static ev_timer **timers = NULL;
Hashtab tab;


void loader_init(struct ev_loop *_loop)
{
	loop = _loop;
	hashtab_init(&tab, LOADER_TAB_SIZE, (free_fun) dir_destroy);
}


void loader_deinit()
{
	cvector_foreach(timer, timers) {
		free(timer);
	}
	cvector_free(timers);
	hashtab_deinit(&tab);
}


static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) revents;
	async_dir_load(w->data, true);
	ev_timer_stop(loop, w);
	free(w);
	cvector_swap_remove(timers, w);
}


static inline void schedule_dir_load(Dir *dir, uint64_t time)
{
	ev_timer *timer = malloc(sizeof *timer);
	ev_timer_init(timer, timer_cb, 0, (time - current_millis()) / 1000.);
	timer->data = dir;
	ev_timer_again(loop, timer);
	cvector_push_back(timers, timer);
}


void loader_reload(Dir *dir)
{
	uint64_t now = current_millis();
	uint64_t latest = dir->next;  // possibly in the future

	if (latest >= now + cfg.inotify_timeout)
		return;  // discard

	// Add a small delay so we don't show files that exist only very briefly
	uint64_t next = now < latest + cfg.inotify_timeout
		? latest + cfg.inotify_timeout + cfg.inotify_delay
		: now + cfg.inotify_delay;
	schedule_dir_load(dir, next);
	dir->next = next;
}


Dir *loader_load_path(const char *path)
{
	char fullpath[PATH_MAX];
	if (path_is_relative(path)) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s", getenv("PWD"), path);
		path = fullpath;
	}

	Dir *dir = hashtab_get(&tab, path);
	if (dir) {
		async_dir_check(dir);
		dir->hidden = cfg.hidden;
		dir_sort(dir);
	} else {
		/* At this point, we should not print this new directory, but
		 * start a timer for, say, 250ms. When the timer runs out we draw the
		 * "loading" directory regardless. The timer should be cancelled when:
		 * 1. the actual directory arrives after loading from disk
		 * 2. we navigate to a different directory (possibly restart a timer there)
		 *
		 * Check how this behaves in the preview pane when just scrolling over
		 * directories.
		 */
		dir = dir_create(path);
		dir->hidden = cfg.hidden;
		hashtab_set(&tab, dir->path, dir);
		async_dir_load(dir, false);
	}
	return dir;
}


void loader_reschedule()
{
	Dir **dirs = NULL;
	cvector_foreach(timer, timers) {
		if (!cvector_contains(dirs, timer->data))
			cvector_push_back(dirs, timer->data);
		ev_timer_stop(loop, timer);
		free(timer);
	}
	cvector_set_size(timers, 0);

	uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

	cvector_foreach(dir, dirs)
		schedule_dir_load(dir, next);
	cvector_free(dirs);
}


Hashtab *loader_hashtab()
{
	return &tab;
}


void loader_drop_cache()
{
	hashtab_clear(&tab);
	cvector_foreach(timer, timers) {
		ev_timer_stop(loop, timer);
		free(timer);
	}
	cvector_set_size(timers, 0);
}
