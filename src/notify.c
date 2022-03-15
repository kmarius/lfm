#include <errno.h>
#include <ev.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "notify.h"
#include "util.h"

#define NOTIFY_EVENTS (IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)

// This is plenty of space, most file names are shorter and as long as
// *one* event fits we should not get overwhelmed
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  // max filename length, arbitrary
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

static struct dir_load_data {
	Dir *dir;
	uint64_t time;
} *dir_load_queue;

struct notify_watcher_data {
	int wd;
	Dir *dir;
	uint64_t next;
};

static int inotify_fd = -1;
static int fifo_wd = -1;
static ev_io inotify_watcher;
static ev_timer dir_load_timer;
static cvector_vector_type(struct notify_watcher_data) watchers = NULL;

static inline struct notify_watcher_data *find_watcher_data(int wd);

#define unwatch(t) \
	do { \
		inotify_rm_watch(inotify_fd, (t).wd); \
	} while (0)


static inline void schedule_timer(struct ev_loop *loop, ev_timer *timer, uint64_t delay)
{
	timer->repeat = delay / 1000.;
	ev_timer_again(loop, timer);
}


static inline void schedule_dir_load(struct ev_loop *loop, ev_timer *timer, Dir *dir, uint64_t time)
{
	cvector_push_back(dir_load_queue, ((struct dir_load_data) {dir, time}));
	if (cvector_size(dir_load_queue) == 1 || dir_load_queue[0].time > time)
		schedule_timer(loop, timer, time - current_millis());
	cvector_upheap_min(dir_load_queue, cvector_size(dir_load_queue) - 1, .time);
}


static void dir_load_timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) w;
	(void) revents;

	if (cvector_size(dir_load_queue) == 0)
		return;

	async_dir_load(dir_load_queue[0].dir, true);
	cvector_swap_erase(dir_load_queue, 0);

	if (cvector_size(dir_load_queue) == 0)
		return;

	cvector_downheap_min(dir_load_queue, 0, .time);

	const uint64_t now = current_millis();
	if (dir_load_queue[0].time <= now)
		dir_load_timer_cb(loop, w, 0);
	else
		schedule_timer(loop, w, dir_load_queue[0].time - now);
}


/* TODO: we currently don't notice if the current directory is deleted while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, int revents)
{
	(void) loop;
	(void) revents;
	App *app = w->data;
	int nread;
	char buf[EVENT_BUFLEN], *p;
	struct inotify_event *event;

	while ((nread = read(inotify_fd, buf, EVENT_BUFLEN)) > 0) {
		for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
			event = (struct inotify_event *) p;

			if (event->len == 0)
				continue;

			// we use inotify for the fifo because io watchers dont seem to work properly
			// with the fifo, the callback gets called every loop, even with clearerr
			/* TODO: we could filter for our pipe here (on 2021-08-13) */
			if (event->wd == fifo_wd) {
				app_read_fifo(app);
				continue;
			}

			struct notify_watcher_data *data = find_watcher_data(event->wd);
			if (!data)
				continue;

			const uint64_t now = current_millis();

			const uint64_t latest = data->next;

			if (latest >= now + cfg.inotify_timeout)
				continue; /* discard */

			/*
			 * add a small delay to address an issue where three reloads are
			 * scheduled when events come in in quick succession
			 */
			const uint64_t next = now < latest + cfg.inotify_timeout
				? latest + cfg.inotify_timeout + cfg.inotify_delay
				: now + cfg.inotify_delay;
			schedule_dir_load(loop, &dir_load_timer, data->dir, next);
			data->next = next;
		}
	}
}


int notify_init(App *app)
{
	inotify_fd = inotify_init1(IN_NONBLOCK);

	if (inotify_fd == -1)
		return -1;

	if ((fifo_wd = inotify_add_watch(inotify_fd, cfg.rundir, IN_CLOSE_WRITE)) == -1) {
		log_error("inotify: %s", strerror(errno));
		return -1;
	}

	ev_io_init(&inotify_watcher, inotify_cb, inotify_fd, EV_READ);
	inotify_watcher.data = app;
	ev_io_start(app->loop, &inotify_watcher);

	ev_init(&dir_load_timer, dir_load_timer_cb);
	dir_load_timer.data = app;

	return inotify_fd;
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

	cvector_push_back(watchers, ((struct notify_watcher_data) {wd, dir, 0}));
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


static inline struct notify_watcher_data *find_watcher_data(int wd)
{
	for (size_t i = 0; i < cvector_size(watchers); i++) {
		if (watchers[i].wd == wd)
			return &watchers[i];
	}
	return NULL;
}


void notify_empty_queue()
{
	cvector_set_size(dir_load_queue, 0);
}


// TODO: maybe just keep loop static here too?
void notify_reschedule(struct ev_loop *loop)
{
	cvector_vector_type(Dir *) dirs = NULL;
	for (size_t i = 0; i < cvector_size(dir_load_queue); i++) {
		if (!cvector_contains(dirs, dir_load_queue[i].dir))
			cvector_push_back(dirs, dir_load_queue[i].dir);
	}
	cvector_set_size(dir_load_queue, 0);

	const uint64_t next = current_millis() + cfg.inotify_timeout + cfg.inotify_delay;

	for (size_t i = 0; i < cvector_size(dirs); i++) {
		for (size_t j = 0; j < cvector_size(watchers); j++) {
			if (watchers[j].dir == dirs[i]) {
				watchers[j].next = next;
				break;
			}
		}
		schedule_dir_load(loop, &dir_load_timer, dirs[i], next);
	}

	cvector_free(dirs);
}


void notify_deinit()
{
	if (inotify_fd == -1)
		return;

	cvector_free(dir_load_queue);

	cvector_ffree(watchers, unwatch);
	watchers = NULL;
	close(inotify_fd);
	inotify_fd = -1;
}
