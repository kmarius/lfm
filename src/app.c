#include <curses.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
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
#include "popen_arr.h"
#include "tpool.h"
#include "ui.h"
#include "util.h"

#define T App

static void add_io_watcher(T *t, FILE* f);
static void app_read_fifo(T *t);

#define TICK 1  /* seconds */

/* Inotify: this is plenty of space, most file names are shorter and as long as
 * *one* event fits we should not get overwhelmed */
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  /* max filename length, arbitrary */
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

static App *_app; /* only needed for print/error and some callbacks :/ */
static const size_t max_threads = 20;
static int fifo_fd = -1;
static int fifo_wd = -1;
static uint64_t input_timeout = 0; /* written by app_timeout, read by stdin_cb  */

static ev_async async_res_watcher;
static ev_idle redraw_watcher;
static ev_io inotify_watcher;
static ev_io stdin_watcher;
static ev_prepare prepare_watcher;
static ev_signal signal_watcher;
static ev_timer timer_watcher;

static cvector_vector_type(ev_io *) io_watchers = NULL; /* to capture stdout and stderr of processes */
static cvector_vector_type(ev_child *) child_watchers = NULL; /* to run callbacks when processes finish */

/* callbacks {{{ */
static void async_result_cb(EV_P_ ev_async *w, int revents)
{
	(void) revents;
	App *app = (App *) w->data;
	struct Result *res;

	pthread_mutex_lock(&async_results.mutex);
	while ((res = resultqueue_get(&async_results)) != NULL) {
		result_callback(res, app);
	}
	pthread_mutex_unlock(&async_results.mutex);

	ev_idle_start(app->loop, &redraw_watcher);
}

static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) revents;
	(void) w;
	static uint16_t tick_ct = 0;
	/* App *app = (App *) w->data; */
	tick_ct++;
	if (tick_ct == 1) {
		return;
	}
	/* log_debug("tick"); */
	/* fm_check_dirs(&app->fm); */
}

static void stdin_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	App *app = (App *) w->data;
	ncinput in;
	notcurses_getc_blocking(app->ui.nc, &in);
	if (current_millis() > input_timeout) {
		/* log_debug("id: %d, shift: %d, ctrl: %d alt %d", in.id, in.shift, in.ctrl, in.alt); */
		lua_handle_key(app->L, ncinput_to_input(&in));
		ev_idle_start(app->loop, &redraw_watcher);
	}
}

static void command_stdout_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	App *app = _app;
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
		cvector_swap_remove(io_watchers, w);
		fclose(w->data);
		free(w);
	}
}

static void app_read_fifo(T *t)
{
	char buf[8192 * 2];
	int nbytes;
	if (fifo_fd > 0) {
		/* TODO: allocate string or use readline or something (on 2021-08-17) */
		while ((nbytes = read(fifo_fd, buf, sizeof(buf))) > 0) {
			buf[nbytes-1] = 0;
			lua_eval(t->L, buf);
		}
		ev_idle_start(t->loop, &redraw_watcher);
	}
}

/* seems like inotify increments watch discriptors, we might have to clean this
 * at some point */
static cvector_vector_type(struct tup_t {
		uint64_t next;
		int wd;
		}) times = NULL;

/* TODO: we currently don't notice if the current directory is delete while
 * empty (on 2021-11-18) */
static void inotify_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	App *app = (App *) w->data;
	int nread;
	char buf[EVENT_BUFLEN], *p;
	struct inotify_event *event;

	while ((nread = read(inotify_fd, buf, EVENT_BUFLEN)) > 0) {
		for (p = buf; p < buf + nread; p += EVENT_SIZE + event->len) {
			event = (struct inotify_event *) p;
			if (event->len == 0) {
				continue;
			}

			// we use inotify for the fifo because io watchers dont seem to work properly
			// with the fifo, the callback gets called every loop, even with clearerr
			if (event->wd == fifo_wd) {
				/* TODO: could filter for our pipe here (on 2021-08-13) */
				app_read_fifo(app);
				continue;
			}

			const size_t l = cvector_size(times);
			size_t i;
			for (i = 0; i < l; i++) {
				if (times[i].wd == event->wd) {
					break;
				}
			}
			const uint64_t now = current_millis();
			if (i >= l) {
				Dir *dir = notify_get_dir(event->wd);
				if (dir != NULL) {
					async_dir_load(dir, true);
					cvector_push_back(times, ((struct tup_t) {now, event->wd}));
				}
			} else {
				uint64_t next = now;
				const uint64_t latest = times[i].next;

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
				Dir *dir = notify_get_dir(event->wd);
				if (dir != NULL) {
					async_dir_load_delayed(dir, true, next - now + NOTIFY_DELAY);
					times[i].next = next + NOTIFY_DELAY;
				}
			}
		}
	}
}

/* To run command line cmds after loop starts. I think it is called back before
 * every other cb. */
static void prepare_cb(EV_P_ ev_prepare *w, int revents)
{
	(void) revents;
	App *app = (App *) w->data;
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
	App *app = (App *) w->data;
	ui_clear(&app->ui);
	lua_run_hook(app->L, "Resized");
	ev_idle_start(app->loop, &redraw_watcher);
}

static void child_cb(EV_P_ ev_child *w, int revents)
{
	(void) revents;
	log_debug("child_cb: %d %d", w->pid, w->rstatus);
	ev_child_stop(EV_A_ w);
	cvector_swap_remove(child_watchers, w);
	lua_run_callback(_app->L, *(int *) w->data, w->rstatus);
	free(w->data);
	free(w);
}

static void redraw_cb(EV_P_ ev_idle *w, int revents)
{
	(void) revents;
	App *app = (App *) w->data;
	ui_draw(&app->ui);
	ev_idle_stop(loop, w);
}

/* callbacks }}} */

void app_init(T *t)
{
	_app = t;
	t->loop = ev_default_loop(EVFLAG_NOENV);

	/* inotify should be available on fm startup */
	if (!notify_init()) {
		log_error("inotify: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	ev_io_init(&inotify_watcher, inotify_cb, inotify_fd, EV_READ);
	inotify_watcher.data = t;
	ev_io_start(t->loop, &inotify_watcher);

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

	t->ui.messages = NULL; /* needed to keep errors on fm startup */
	fm_init(&t->fm);
	ui_init(&t->ui, &t->fm);

	ev_idle_init(&redraw_watcher, redraw_cb);
	redraw_watcher.data = t;
	ev_idle_start(t->loop, &redraw_watcher);

	ev_prepare_init(&prepare_watcher, prepare_cb);
	prepare_watcher.data = t;
	ev_prepare_start(t->loop, &prepare_watcher);

	ev_async_init(&async_res_watcher, async_result_cb);
	async_res_watcher.data = t;
	ev_async_start(t->loop, &async_res_watcher);

	resultqueue_init(&async_results, &async_res_watcher);
	ev_async_send(EV_DEFAULT_ &async_res_watcher); /* results will arrive before the loop starts */

	ev_timer_init(&timer_watcher, timer_cb, 0., TICK);
	timer_watcher.data = t;
	ev_timer_start(t->loop, &timer_watcher);

	ev_io_init(&stdin_watcher, stdin_cb, notcurses_inputready_fd(t->ui.nc), EV_READ);
	stdin_watcher.data = t;
	ev_io_start(t->loop, &stdin_watcher);

	ev_signal_init(&signal_watcher, sigwinch_cb, SIGWINCH);
	signal_watcher.data = t;
	ev_signal_start(t->loop, &signal_watcher);

	t->L = luaL_newstate();
	lua_init(t->L, t);
	lua_load_file(t->L, cfg.corepath);

	log_info("initialized app");
}

void app_run(T *t)
{
	ev_run(t->loop, 0);
}

void app_quit(T *t)
{
	lua_run_hook(t->L, "ExitPre");
	ev_break(t->loop, EVBREAK_ALL);
}

static void add_io_watcher(T *t, FILE* f)
{
	if (f == NULL) {
		return;
	}
	log_debug("adding io watcher %d", fileno(f));
	ev_io *w = malloc(sizeof(ev_io));
	int flags = fcntl(fileno(f), F_GETFL, 0);
	fcntl(fileno(f), F_SETFL, flags | O_NONBLOCK);
	ev_io_init(w, command_stdout_cb, fileno(f), EV_READ);
	w->data = f;
	ev_io_start(t->loop, w);
	cvector_push_back(io_watchers, w);
}

static void add_child_watcher(T *t, int pid, int key)
{
	log_debug("adding child watcher %d", pid);
	ev_child *w = malloc(sizeof(ev_child));
	ev_child_init(w, child_cb, pid, 0);
	w->data = malloc(sizeof(int));
	*(int *) w->data = key;
	ev_child_start(t->loop, w);
	cvector_push_back(child_watchers, w);
}

bool app_execute(T *t, const char *prog, char *const *args, bool forking, bool out, bool err, int key)
{
	FILE *fout, *ferr;
	int pid, status, rc;
	log_debug("execute: %s %d %d", prog, out, err);
	if (forking) {
		pid = popen2_arr_p(NULL, &fout, &ferr, prog, args, NULL);
		if (err) {
			add_io_watcher(t, ferr);
		} else {
			fclose(ferr);
		}
		if (out) {
			add_io_watcher(t, fout);
		} else {
			fclose(fout);
		}
		if (key > 0) {
			add_child_watcher(t, pid, key);
		}
		return pid != -1;
	} else {
		ui_suspend(&t->ui);
		kbblocking(true);
		if ((pid = fork()) < 0) {
			status = -1;
		} else if (pid == 0) {
			/* child */
			execvp(prog, (char* const *) args);
			_exit(127); /* execl error */
		} else {
			/* parent */
			do {
				rc = waitpid(pid, &status, 0);
			} while ((rc == -1) && (errno == EINTR));
		}
		kbblocking(false);
		ui_notcurses_init(&t->ui);
		t->ui.redraw.fm = 1;
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

void timeout_set(uint16_t duration)
{
	input_timeout = current_millis() + duration;
}

void app_deinit(T *t)
{
	if (t == NULL) {
		return;
	}
	cvector_free(times);
	cvector_ffree(io_watchers, free);
	cvector_ffree(child_watchers, free);
	notify_close();
	lua_deinit(t->L);
	ui_deinit(&t->ui);
	fm_deinit(&t->fm);
	tpool_wait(async_tm);
	tpool_destroy(async_tm);
	resultqueue_deinit(&async_results);
	pthread_mutex_destroy(&async_results.mutex);
	if (fifo_fd > 0) {
		close(fifo_fd);
	}
	remove(cfg.fifopath);
}

#undef T
