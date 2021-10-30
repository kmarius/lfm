#include <curses.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <pthread.h>
#include <notcurses/notcurses.h>
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

#define TICK 1  /* seconds */

/* Inotify: this is plenty of space, most file names are shorter and as long as
 * *one* event fits we should not get overwhelmed */
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  /* max filename length, arbitrary */
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

static app_t *_app;
static const size_t max_threads = 20;
static int fifo_fd = -1;
static int fifo_wd = -1;
static unsigned long input_timeout = 0; /* written by app_timeout, read by stdin_cb  */

static ev_io inotify_watcher;
static ev_idle redraw_watcher;
static ev_prepare prepare_watcher;
static ev_timer timer_watcher;
static ev_io stdin_watcher;
static ev_signal signal_watcher;
static ev_async async_res_watcher;

/* callbacks {{{ */
static void async_result_cb(EV_P_ ev_async *w, int revents)
{
	(void)revents;
	app_t *app = (app_t*) w->data;
	bool redraw, redraw_preview;
	void *result;
	enum result_e type;

	redraw = false, redraw_preview = false;
	pthread_mutex_lock(&async_results.mutex);
	while (queue_get(&async_results, &type, &result)) {
		switch (type) {
		case RES_DIR:
			redraw |= fm_insert_dir(&app->fm, result);
			break;
		case RES_PREVIEW:
			redraw_preview |= ui_insert_preview(&app->ui, result);
			break;
		default:
			break;
		}
	}
	pthread_mutex_unlock(&async_results.mutex);

	if (redraw) {
		app->ui.redraw.fm = 1;
	} else if (redraw_preview) {
		app->ui.redraw.preview = 1;
	}
	app_restart_redraw_watcher(app);
}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) w;
	(void) revents;
	static int tick_ct = 0;
	/* app_t *app = (app_t *)w->data; */
	tick_ct++;
	if (tick_ct == 1) {
		return;
	}
	/* log_debug("tick"); */
	/* fm_check_dirs(&app->fm); */
}

static void stdin_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	(void)w;
	app_t *app = (app_t *)w->data;
	ncinput in;
	notcurses_getc_blocking(app->ui.nc, &in);
	if (current_millis() > input_timeout) {
		/* log_debug("%u", in.id); */
		lua_handle_key(app->L, app, &in);
		app_restart_redraw_watcher(app);
	}
}

static void read_fifo(app_t *app)
{
	char buf[8192 * 2];
	int nbytes;
	if (fifo_fd > 0) {
		/* TODO: allocate string or use readline or something (on 2021-08-17) */
		while ((nbytes = read(fifo_fd, buf, sizeof(buf))) > 0) {
			buf[nbytes-1] = 0;
			lua_exec_expr(app->L, app, buf);
		}
		app_restart_redraw_watcher(app);
	}
}

struct tup_t {
	unsigned long
		next; /* time of the next planned scan, could lie in the past */
	int wd;
};

/* seems like inotify increments watch discriptors, we might have to clean this
 * at some point */
static cvector_vector_type(struct tup_t) times = NULL;

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
				read_fifo(app);
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
					async_dir_load(p);
					struct tup_t t = { .next = now, .wd = event->wd, };
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
					async_dir_load_delayed(
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
	(void) loop;
	app_t *app = (app_t*) w->data;
	if (cfg.commands) {
		for (size_t i = 0; i < cvector_size(cfg.commands); i++) {
			lua_exec_expr(app->L, app, cfg.commands[i]);
		}
		/* commands are from argv, don't free them */
		cvector_free(cfg.commands);
		cfg.commands = NULL;
	}
	lua_run_hook(app->L, "LfmEnter");
	ev_prepare_stop(loop, w);
}

static void sigwinch_cb(EV_P_ ev_signal *w, int revents)
{
	(void) revents;
	app_t *app = (app_t *)w->data;
	ui_clear(&app->ui);
	app_restart_redraw_watcher(app);
}

static void redraw_cb(struct ev_loop *loop, ev_idle *w, int revents)
{
	(void) revents;
	app_t *app = (app_t*) w->data;
	ui_draw(&app->ui);
	ev_idle_stop(loop, w);
}

/* callbacks }}} */

void app_init(app_t *app)
{
	_app = app;
	app->loop = ev_default_loop(EVFLAG_NOENV);

	/* inotify should be available on fm startup */
	if (!notify_init()) {
		log_error("inotify: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	ev_io_init(&inotify_watcher, inotify_cb, inotify_fd, EV_READ);
	inotify_watcher.data = app;
	ev_io_start(app->loop, &inotify_watcher);

	const size_t nthreads = min(get_nprocs()+1, max_threads);
	async_tm = tpool_create(nthreads);
	log_info("initialized pool of %d threads", nthreads);

	if (pthread_mutex_init(&async_results.mutex, NULL) != 0) {
		exit(EXIT_FAILURE);
	}

	if (mkdir(cfg.rundir, 0700) == -1 && errno != EEXIST) {
		fprintf(stderr, "mkdir: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if ((mkfifo(cfg.fifopath, 0600) == -1 && errno != EEXIST) ||
			(fifo_fd = open(cfg.fifopath, O_RDONLY|O_NONBLOCK, 0)) == -1) {
		log_error("fifo: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	setenv("LFMFIFO", cfg.fifopath, 1);

	if ((fifo_wd = inotify_add_watch(inotify_fd, cfg.rundir, IN_CLOSE_WRITE)) == -1) {
		log_error("inotify: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	app->ui.messages = NULL; /* needed to keep errors on fm startup */
	fm_init(&app->fm);
	ui_init(&app->ui, &app->fm);

	ev_idle_init(&redraw_watcher, redraw_cb);
	redraw_watcher.data = app;
	ev_idle_start(app->loop, &redraw_watcher);

	ev_prepare_init(&prepare_watcher, prepare_cb);
	prepare_watcher.data = app;
	ev_prepare_start(app->loop, &prepare_watcher);

	ev_async_init(&async_res_watcher, async_result_cb);
	async_res_watcher.data = app;
	ev_async_start(app->loop, &async_res_watcher);

	async_results.watcher = &async_res_watcher;
	ev_async_send(EV_DEFAULT_ &async_res_watcher); /* results will arrive before the loop starts */

	ev_timer_init(&timer_watcher, timer_cb, 0., TICK);
	timer_watcher.data = app;
	ev_timer_start(app->loop, &timer_watcher);

	ev_io_init(&stdin_watcher, stdin_cb, notcurses_inputready_fd(app->ui.nc), EV_READ);
	stdin_watcher.data = app;
	ev_io_start(app->loop, &stdin_watcher);

	ev_signal_init(&signal_watcher, sigwinch_cb, SIGWINCH);
	signal_watcher.data = app;
	ev_signal_start(app->loop, &signal_watcher);

	app->L = luaL_newstate();
	lua_init(app->L, app);
	/* TODO: show errors in ui (on 2021-08-04) */
	lua_load_file(app->L, app, cfg.corepath);

	log_info("initialized app");
}

void app_restart_redraw_watcher(app_t *app)
{
	ev_idle_start(app->loop, &redraw_watcher);
}

void app_run(app_t *app)
{
	ev_run(app->loop, 0);
}

void app_quit(app_t *app)
{
	lua_run_hook(app->L, "ExitPre");
	ev_break(app->loop, EVBREAK_ALL);
}

void print(const char *format, ...)
{
	if (!_app) {
		return;
	}
	va_list args;
	va_start(args, format);
	ui_vechom(&_app->ui, format, args);
	va_end(args);
}

void error(const char *format, ...)
{
	if (! _app) {
		return;
	}
	va_list args;
	va_start(args, format);
	ui_verror(&_app->ui, format, args);
	va_end(args);
}

void timeout_set(int duration)
{
	input_timeout = current_millis() + duration;
}

void app_deinit(app_t *app)
{
	cvector_free(times);
	notify_close();
	lua_deinit(app->L);
	ui_deinit(&app->ui);
	fm_deinit(&app->fm);
	tpool_wait(async_tm);
	tpool_destroy(async_tm);
	queue_deinit(&async_results);
	pthread_mutex_destroy(&async_results.mutex);
	if (fifo_fd > 0) {
		close(fifo_fd);
	}
	remove(cfg.fifopath);
}
