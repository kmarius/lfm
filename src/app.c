#include <curses.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"
#include "async.h"
#include "config.h"
#include "keys.h"
#include "log.h"
#include "lualfm.h"
#include "notify.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"
#include "popen_arr.h"

static void add_io_watcher(app_t *app, FILE* f);

static cvector_vector_type(ev_io*) io_watchers = NULL;

#define TICK 1  /* seconds */

/* Inotify: this is plenty of space, most file names are shorter and as long as
 * *one* event fits we should not get overwhelmed */
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  /* max filename length, arbitrary */
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

static app_t *_app; /* only needed for print/error */
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
	res_t result;

	pthread_mutex_lock(&async_results.mutex);
	while (queue_get(&async_results, &result))
		result.cb(&result, app);
	pthread_mutex_unlock(&async_results.mutex);

	ev_idle_start(app->loop, &redraw_watcher);
}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) revents;
	(void) w;
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
	app_t *app = (app_t *)w->data;
	ncinput in;
	notcurses_getc_blocking(app->ui.nc, &in);
	if (current_millis() > input_timeout) {
		/* log_debug("id: %d, shift: %d, ctrl: %d alt %d", in.id, in.shift, in.ctrl, in.alt); */
		lua_handle_key(app->L, ncinput_to_long(&in));
		ev_idle_start(app->loop, &redraw_watcher);
	}
}

static void command_stdout_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	/* app_t *app = w->data; */
	app_t *app = _app;
	char *line = NULL;
	int read;
	size_t n;

	while ((read = getline(&line, &n, w->data)) != -1) {
		if (line[read-1] == '\n') {
			line[read-1] = 0;
		}
		/* TODO: strip special chars? (on 2021-11-14) */
		ui_echom(&app->ui, "%s", line);
		log_debug("%s", line);
		free(line);
		line = NULL;
	}
	/* log_debug("%d %s %d", read, strerror(errno), errno); */
	free(line);

	if (errno == EAGAIN) {
		/* this seems to prevent the callback being immediately called again */
		clearerr(w->data);
	}

	if (errno == ECHILD || feof(w->data)) {
		log_debug("removing watcher %d", w->fd);
		ev_io_stop(app->loop, w);
		size_t i;
		for (i = 0; i < cvector_size(io_watchers); i++) {
			if (io_watchers[i] == w) {
				cvector_swap_erase(io_watchers, i);
				break;
			}
		}
		fclose(w->data);
		free(w);
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
			lua_eval(app->L, buf);
		}
		ev_idle_start(app->loop, &redraw_watcher);
	}
}

struct tup_t {
	unsigned long next; /* time of the last scheduled scan, could lie in the past or the future */
	int wd;
};

/* seems like inotify increments watch discriptors, we might have to clean this
 * at some point */
static cvector_vector_type(struct tup_t) times = NULL;

/* TODO: we currently don't notice if the current directory is delete while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	app_t *app = (app_t*) w->data;
	int nread;
	size_t i;
	char buf[EVENT_BUFLEN], *p;
	struct inotify_event *event;

	while ((nread = read(inotify_fd, buf, EVENT_BUFLEN)) > 0) {
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
				dir_t *dir = notify_get_dir(event->wd);
				if (dir != NULL) {
					async_dir_load(dir, true);
					struct tup_t t = { .next = now, .wd = event->wd, };
					cvector_push_back(times, t);
				} else {
					log_warn("notify event for unloaded dir");
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
				dir_t *dir = notify_get_dir(event->wd);
				if (dir != NULL) {
					async_dir_load_delayed(dir, true, next - now + NOTIFY_DELAY);
					times[i].next = next + NOTIFY_DELAY;
				} else {
					log_warn("notify event for unknown dir");
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
	if (cfg.commands != NULL) {
		for (size_t i = 0; i < cvector_size(cfg.commands); i++) {
			lua_eval(app->L, cfg.commands[i]);
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
	lua_run_hook(app->L, "Resized");
	ev_idle_start(app->loop, &redraw_watcher);
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
	lua_load_file(app->L, cfg.corepath);

	log_info("initialized app");
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

static void add_io_watcher(app_t *app, FILE* f)
{
	if (f == NULL) {
		return;
	}
	log_debug("adding watcher %d", fileno(f));
	ev_io *w = malloc(sizeof(ev_io));
	int flags = fcntl(fileno(f), F_GETFL, 0);
	fcntl(fileno(f), F_SETFL, flags | O_NONBLOCK);
	ev_io_init(w, command_stdout_cb, fileno(f), EV_READ);
	w->data = f;
	ev_io_start(app->loop, w);
	cvector_push_back(io_watchers, w);
}

bool app_execute(app_t *app, const char *prog, char *const *args, bool forking, bool out, bool err)
{
	FILE *fout, *ferr;
	int pid, status, rc;
	log_debug("execute: %s %d %d", prog, out, err);
	if (forking) {
		pid = popen2_arr_p(NULL, &fout, &ferr, prog, args, NULL);
		if (err) {
			add_io_watcher(app, ferr);
		} else {
			fclose(ferr);
		}
		if (out) {
			add_io_watcher(app, fout);
		} else {
			fclose(fout);
		}
		/* TODO: for native callbacks, enable a watcher for the child here (on 2022-01-01) */
		return pid != -1;
	} else {
		ui_suspend(&app->ui);
		kbblocking(true);
		/* TODO: probably needs signal handling of some kind (on 2022-01-02) */
		if ((pid = fork()) < 0) {
			status = -1;
		} else if (pid == 0) {
			/* child */
			execvp(prog, (char* const*) args);
			_exit(127); /* execl error */
		} else {
			/* parent */
			do {
				rc = waitpid(pid, &status, 0);
			} while ((rc == -1) && (errno == EINTR));
		}
		kbblocking(false);
		ui_notcurses_init(&app->ui);
		app->ui.redraw.fm = 1;
		return status == 0;
	}
}

void print(const char *format, ...)
{
	if (_app == NULL) {
		return;
	}
	va_list args;
	va_start(args, format);
	ui_vechom(&_app->ui, format, args);
	va_end(args);
}

void error(const char *format, ...)
{
	if (_app == NULL) {
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
	cvector_ffree(io_watchers, free);
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
