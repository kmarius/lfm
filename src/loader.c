#include "app.h"
#include "async.h"
#include "config.h"
#include "dir.h"
#include "hashtab.h"
#include "loader.h"

static ev_timer timer_watcher;
static struct ev_loop *loop = NULL;
Hashtab tab;

static struct dir_load_data {
	Dir *dir;
	uint64_t time;
} *dir_load_queue = NULL;

static void dir_load_timer_cb(EV_P_ ev_timer *w, int revents);


void loader_init(App *app)
{
	loop = app->loop;
	hashtab_init(&tab, LOADER_TAB_SIZE, (free_fun) dir_destroy);

	ev_init(&timer_watcher, dir_load_timer_cb);
	timer_watcher.data = app;
}


void loader_deinit()
{
	cvector_free(dir_load_queue);
	hashtab_deinit(&tab);
}


static inline void schedule_timer(struct ev_loop *loop, ev_timer *timer, uint64_t delay)
{
	timer->repeat = delay / 1000.;
	ev_timer_again(loop, timer);
}


static inline void schedule_dir_load(Dir *dir, uint64_t time)
{
	cvector_push_back(dir_load_queue, ((struct dir_load_data) {dir, time}));
	if (cvector_size(dir_load_queue) == 1 || dir_load_queue[0].time > time)
		schedule_timer(loop, &timer_watcher, time - current_millis());
	cvector_upheap_min(dir_load_queue, cvector_size(dir_load_queue) - 1, .time);
}


static void dir_load_timer_cb(EV_P_ ev_timer *w, int revents)
{
	if (cvector_size(dir_load_queue) == 0)
		return;

	async_dir_load(dir_load_queue[0].dir, true);
	cvector_swap_erase(dir_load_queue, 0);

	if (cvector_size(dir_load_queue) == 0)
		return;

	cvector_downheap_min(dir_load_queue, 0, .time);

	uint64_t now = current_millis();
	if (dir_load_queue[0].time <= now)
		dir_load_timer_cb(loop, w, revents);
	else
		schedule_timer(loop, &timer_watcher, dir_load_queue[0].time - now);
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
	for (size_t i = 0; i < cvector_size(dir_load_queue); i++) {
		if (!cvector_contains(dirs, dir_load_queue[i].dir))
			cvector_push_back(dirs, dir_load_queue[i].dir);
	}
	cvector_set_size(dir_load_queue, 0);

	uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

	for (size_t i = 0; i < cvector_size(dirs); i++)
		schedule_dir_load(dirs[i], next);

	cvector_free(dirs);
}


Hashtab *loader_hashtab()
{
	return &tab;
}


void loader_drop_cache()
{
	hashtab_clear(&tab);
	cvector_set_size(dir_load_queue, 0);
}
