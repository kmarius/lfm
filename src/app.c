#include <curses.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <pthread.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/sysinfo.h>

#include "app.h"
#include "notify.h"
#include "async.h"
#include "config.h"
#include "keys.h"
#include "log.h"
#include "lualfm.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

/* Inotify: this is plenty of space, most file names are shorter and as long as
 * *one* event fits we should not get overwhelmed */
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128 /* max filename length, arbitrary */
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

#define TICK 5

extern tpool_t *tm; /* tpool.h */
extern resq_t results; /* async.h */

static app_t *_app;
static const size_t max_threads = 20;
static int fifo_fd = -1;
static int fifo_wd = -1;
static unsigned long input_timeout = 0; /* written by app_timeout, checked by stdin_cb  */

/* callbacks {{{ */
static void async_result_cb(EV_P_ ev_async *w, int revents)
{
	(void)revents;
	app_t *app = (app_t*) w->data;
	bool redraw, redraw_preview;
	res_t *result;

	redraw = false, redraw_preview = false;
	pthread_mutex_lock(&results.mutex);
	while (queue_get(&results, &result)) {
		switch (result->type) {
		case RES_DIR:
			/* log_debug("queue: dir %s %p", ((dir_t*)result->payload)->path, result->payload); */
			redraw |=
			    nav_insert_dir(&app->nav, (dir_t *)result->payload);
			break;
		case RES_PREVIEW:
			redraw_preview |= ui_insert_pv(
			    &app->ui, (preview_t *)result->payload);
			/* log_debug("queue: preview %s %p", ((preview_t*)result->payload)->path, result); */
			break;
		default:
			break;
		}
		free(result);
	}
	pthread_mutex_unlock(&results.mutex);

	if (redraw) {
		ui_draw(&app->ui, &app->nav);
	} else if (redraw_preview) {
		ui_draw_preview(&app->ui);
	}
}

static void sigwinch_cb(EV_P_ ev_signal *w, int revents)
{
	(void)revents;
	app_t *app = (app_t *)w->data;
	log_debug("resize");
	clear();
	erase();
	refresh();
	ui_resize(&app->ui);
	ui_draw(&app->ui, &app->nav);
}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) w;
	(void) revents;
	static int tick_ct = 0;
	tick_ct++;
	if (tick_ct-1 == 0) {
		return;
	}
	/* log_debug("tick"); */
	/* app_t *app = (app_t *)w->data; */
	/* nav_check_dirs(&app->nav); */
}

static void stdin_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	(void)w;
	app_t *app = (app_t *)w->data;
	int key2, key1 = getch();
	if (key1 == 27 && (key2 = getch()) != ERR) {
		key1 = ALT(key2);
	}
	log_trace("stdin_cb: %d, %s", key1, keyname(key1));

	if (current_millis() > input_timeout) {
		lua_handle_key(app->L, app, key1);
	}
}

typedef struct Tup {
	unsigned long
		next; /* time of the next planned scan, could lie in the past */
	int wd;
} tup_t;

/* seems like inotify increments watch discriptors, we might have to clean this
 * at some point */
static cvector_vector_type(tup_t) times = NULL;

static void read_pipe(app_t *app)
{
	char buf[4096];
	int nbytes;
	if (fifo_fd > 0) {
		while ((nbytes = read(fifo_fd, buf, sizeof(buf))) > 0) {
			buf[nbytes-1] = 0;
			log_debug("read %d bytes from fifo: %s", nbytes, buf);
			lua_exec_lfmcmd(app->L, app, buf);
		}
	}
}

static void inotify_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	(void)w;
	app_t *app = (app_t*) w->data;
	int nread;
	size_t i;
	char buf[EVENT_BUFLEN], *p;
	struct inotify_event *event;

	while ((nread = read(inotify_fd, buf, EVENT_BUFLEN)) > 0) {
		/* if we log on every iteration we will get stuck in the directory
		 * containing the log */
		/* log_debug("inotify_cb %d bytes read", nread); */

		for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
			event = (struct inotify_event *)p;
			if (event->len == 0) {
				continue;
			}

			if (event->wd == fifo_wd) {
				/* TODO: could filter for our pipe here (on 2021-08-13) */
				read_pipe(app);
				continue;
			}

			const size_t l = cvector_size(times);
			for (i = 0; i < l; i++) {
				if (times[i].wd == event->wd) {
					break;
				}
			}
			const unsigned long now = current_millis();
			if (i >= l) {
				const char *p = notify_get_path(event->wd);
				if (p != NULL) {
					async_load_dir(p);
					tup_t t = {.next = now,
						.wd = event->wd};
					cvector_push_back(times, t);
				}
			} else {
				unsigned long next = now;
				const unsigned long latest = times[i].next;

				if (latest >= now + NOTIFY_TIMEOUT) {
					/* discard */
					continue;
				}

				if (now > latest) {
					if (latest + NOTIFY_TIMEOUT > now) {
						next = latest + NOTIFY_TIMEOUT;
					}
				} else {
					next = latest + NOTIFY_TIMEOUT;
				}

				/* add a small delay to address an issue where
				 * three reloads are
				 * scheduled when events come in in quick
				 * succession */
				const char *path = notify_get_path(event->wd);
				if (path) {
					async_load_dir_delayed(
							path, next - now + NOTIFY_DELAY);
					times[i].next = next + NOTIFY_DELAY;
					/* log_debug("loading %s at %lu", p,
					 * times[i].next); */
				}
			}
		}
	}
}

/* To run command line cmds after loop starts. I think it is called back before
 * every other cb. */
static void prepare_cb(struct ev_loop *loop, ev_prepare *w, int revents)
{
	(void) revents;
	app_t *app = (app_t*) w->data;
	if (cfg.commands) {
		size_t i;
		for (i = 0; i < cvector_size(cfg.commands); i++) {
			lua_exec_lfmcmd(app->L, app, cfg.commands[i]);
		}
		/* commands are from argv, don't free them */
		cvector_free(cfg.commands);
		cfg.commands = NULL;
	}
	lua_run_hook(app->L, "StartupComplete");
	ev_prepare_stop(loop, w);
}

/* callbacks }}} */

static ev_io inotify_watcher;
static ev_prepare prepare_watcher;
static ev_timer timer_watcher;
static ev_io stdin_watcher;
static ev_signal signal_watcher;
static ev_async async_res_watcher;

void app_init(app_t *app)
{
	app->loop = ev_default_loop(EVFLAG_NOENV);

	/* inotify should be available on nav startup */
	if (!notify_init()) {
		log_error("inotify: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* special watcher for the fifo */
	if ((fifo_wd = inotify_add_watch(inotify_fd, cfg.rundir, IN_CLOSE_WRITE)) == -1) {
		log_error("inotify: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	ev_io_init(&inotify_watcher, inotify_cb, inotify_fd, EV_READ);
	inotify_watcher.data = app;
	ev_io_start(app->loop, &inotify_watcher);

	const size_t nthreads = min(get_nprocs()+1, max_threads);
	tm = tpool_create(nthreads);
	log_debug("initialized pool of %d threads", nthreads);

	results.head = NULL;
	results.tail = NULL;
	results.watcher = NULL;

	int rc = pthread_mutex_init(&results.mutex, NULL);
	assert(rc == 0);
	(void) rc;

	ui_init(&app->ui);
	nav_init(&app->nav);
	/* app->ui.nav = &app->nav; */

	app->L = luaL_newstate();
	lualfm_init(app->L, app);

	/* Load config */
	/* TODO: show errors in ui (on 2021-08-04) */
	lua_load_file(app->L, app, cfg.corepath);

	ev_prepare_init(&prepare_watcher, prepare_cb);
	prepare_watcher.data = app;
	ev_prepare_start(app->loop, &prepare_watcher);

	ev_async_init(&async_res_watcher, async_result_cb);
	async_res_watcher.data = app;
	results.watcher = &async_res_watcher;
	ev_async_start(app->loop, &async_res_watcher);
	ev_async_send(EV_DEFAULT_ &async_res_watcher); /* results will arrive before the loop starts */

	ev_timer_init(&timer_watcher, timer_cb, 0., TICK);
	timer_watcher.data = app;
	ev_timer_start(app->loop, &timer_watcher);

	ev_io_init(&stdin_watcher, stdin_cb, STDIN_FILENO, EV_READ);
	stdin_watcher.data = app;
	ev_io_start(app->loop, &stdin_watcher);

	ev_signal_init(&signal_watcher, sigwinch_cb, SIGWINCH);
	signal_watcher.data = app;
	ev_signal_start(app->loop, &signal_watcher);

	if ((mkfifo(cfg.fifopath, 0600) == -1 && errno != EEXIST) ||
			(fifo_fd = open(cfg.fifopath, O_RDONLY|O_NONBLOCK, 0)) == -1) {
		log_error("fifo: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	_app = app;
	log_info("initialized app");
}

void run(app_t *app) { ev_run(app->loop, 0); }

void app_quit(app_t *app) { ev_break(app->loop, EVBREAK_ALL); }

void print(const char *format, ...)
{
	if (! _app) {
		return;
	}
	char msg[1024];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	ui_echom(&_app->ui, msg);
}

void error(const char *format, ...)
{
	if (! _app) {
		return;
	}
	char msg[1024];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	ui_error(&_app->ui, msg);
}

void app_timeout(int duration) { input_timeout = current_millis() + duration; }

void app_destroy(app_t *app)
{
	cvector_free(times);
	notify_close();
	lua_close(app->L);
	ui_destroy(&app->ui);
	nav_destroy(&app->nav);
	tpool_wait(tm);
	tpool_destroy(tm);
	queue_clear(&results);
	pthread_mutex_destroy(&results.mutex);
	if (fifo_fd > 0) {
		close(fifo_fd);
	}
	remove(cfg.fifopath);
}
