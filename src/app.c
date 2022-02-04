#include <errno.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <notcurses/notcurses.h>
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
#include "util.h"

#define T App

static ev_io *add_io_watcher(T *t, FILE* f);
static void app_read_fifo(T *t);

#define TICK 1  /* seconds */

/* Inotify: this is plenty of space, most file names are shorter and as long as
 * *one* event fits we should not get overwhelmed */
#define EVENT_MAX 8
#define EVENT_MAX_LEN 128  /* max filename length, arbitrary */
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUFLEN (EVENT_MAX * (EVENT_SIZE + EVENT_MAX_LEN))

static App *_app; /* only needed for print/error */
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

static cvector_vector_type(ev_child *) child_watchers = NULL; /* to run callbacks when processes finish */

/* callbacks {{{ */
static void async_result_cb(EV_P_ ev_async *w, int revents)
{
	(void) revents;
	App *app = w->data;
	struct Result *res;

	pthread_mutex_lock(&async_results.mutex);
	while ((res = resultqueue_get(&async_results)))
		result_callback(res, app);
	pthread_mutex_unlock(&async_results.mutex);

	ev_idle_start(loop, &redraw_watcher);
}


static void timer_cb(EV_P_ ev_timer *w, int revents)
{
	(void) loop;
	(void) revents;
	(void) w;
	static uint16_t tick_ct = 0;
	/* App *app = w->data; */

	if (tick_ct++ == 1)
		return;

	log_debug("tick");
}


static void stdin_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	App *app = w->data;
	ncinput in;

	notcurses_getc_blocking(app->ui.nc, &in);

	if (current_millis() <= input_timeout)
		return;

	/* log_debug("id: %d, shift: %d, ctrl: %d alt %d", in.id, in.shift, in.ctrl, in.alt); */
	lua_handle_key(app->L, ncinput_to_input(&in));
	ev_idle_start(loop, &redraw_watcher);
}


struct stdout_watcher_data {
	App *app;
	FILE *stream;
};


static void command_stdout_cb(EV_P_ ev_io *w, int revents)
{
	(void) revents;
	struct stdout_watcher_data *data = w->data;

	char *line = NULL;
	int read;
	size_t n;

	while ((read = getline(&line, &n, data->stream)) != -1) {
		if (line[read-1] == '\n')
			line[read-1] = 0;

		/* TODO: strip special chars? (on 2021-11-14) */
		ui_echom(&data->app->ui, "%s", line);
		free(line);
		line = NULL;
	}
	free(line);

	/* this seems to prevent the callback being immediately called again by libev */
	if (errno == EAGAIN)
		clearerr(data->stream);

	ev_idle_start(loop, &redraw_watcher);
}


static void app_read_fifo(T *t)
{
	char buf[8192 * 2];
	int nbytes;

	if (fifo_fd <= 0)
		return;

	/* TODO: allocate string or use readline or something (on 2021-08-17) */
	while ((nbytes = read(fifo_fd, buf, sizeof(buf))) > 0) {
		buf[nbytes-1] = 0;
		lua_eval(t->L, buf);
	}
	ev_idle_start(t->loop, &redraw_watcher);
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

			struct notify_watcher_data *data = notify_get_watcher_data(event->wd);
			if (!data)
				continue;

			const uint64_t now = current_millis();

			const uint64_t latest = data->next;

			if (latest >= now + NOTIFY_TIMEOUT)
				continue; /* discard */

			/*
			 * add a small delay to address an issue where three reloads are
			 * scheduled when events come in in quick succession
			 */
			const uint64_t next = now < latest + NOTIFY_TIMEOUT
				? latest + NOTIFY_TIMEOUT + NOTIFY_DELAY
				: now + NOTIFY_DELAY;
			async_dir_load_delayed(data->dir, true, next - now);
			data->next = next;
		}
	}
}


/* To run command line cmds after loop starts. I think it is called back before
 * every other cb. */
static void prepare_cb(EV_P_ ev_prepare *w, int revents)
{
	(void) revents;
	App *app = w->data;

	if (cfg.commands) {
		for (size_t i = 0; i < cvector_size(cfg.commands); i++)
			lua_eval(app->L, cfg.commands[i]);

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
	App *app = w->data;
	ui_clear(&app->ui);
	lua_run_hook(app->L, "Resized");
	ev_idle_start(loop, &redraw_watcher);
}


struct child_watcher_data {
	App *app;
	int cb_index;
	ev_io *stdout_watcher;
	ev_io *stderr_watcher;
};


static void child_cb(EV_P_ ev_child *w, int revents)
{
	(void) revents;
	struct child_watcher_data *data = w->data;

	ev_child_stop(EV_A_ w);

	cvector_swap_remove(child_watchers, w);
	if (data->cb_index > 0)
		lua_run_callback(data->app->L, data->cb_index, w->rstatus);

	if (data->stdout_watcher) {
		ev_io_stop(loop, data->stdout_watcher);
		free(data->stdout_watcher->data);
		free(data->stdout_watcher);
	}

	if (data->stderr_watcher) {
		ev_io_stop(loop, data->stderr_watcher);
		free(data->stderr_watcher->data);
		free(data->stderr_watcher);
	}

	free(data);
	free(w);
}


static void redraw_cb(EV_P_ ev_idle *w, int revents)
{
	(void) revents;
	App *app = w->data;
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
		log_error("pthread_mutex_init: %s", strerror(errno));
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
	/* ev_timer_start(t->loop, &timer_watcher); */

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


static ev_io *add_io_watcher(T *t, FILE* f)
{
	if (!f)
		return NULL;

	ev_io *w = malloc(sizeof(ev_io));
	int flags = fcntl(fileno(f), F_GETFL, 0);
	fcntl(fileno(f), F_SETFL, flags | O_NONBLOCK);
	ev_io_init(w, command_stdout_cb, fileno(f), EV_READ);

	struct stdout_watcher_data *data = malloc(sizeof(struct stdout_watcher_data));
	data->app = t;
	data->stream = f;
	w->data = data;

	ev_io_start(t->loop, w);

	return w;
}


static void add_child_watcher(T *t, int pid, int cb_index, ev_io *stdout_watcher, ev_io *stderr_watcher)
{
	ev_child *w = malloc(sizeof(ev_child));
	ev_child_init(w, child_cb, pid, 0);

	struct child_watcher_data *data = malloc(sizeof(struct child_watcher_data));
	data->cb_index = cb_index > 0 ? cb_index : 0;
	data->app = t;
	data->stderr_watcher = stderr_watcher;
	data->stdout_watcher = stdout_watcher;
	w->data = data;

	ev_child_start(t->loop, w);
	cvector_push_back(child_watchers, w);
}


bool app_execute(T *t, const char *prog, char *const *args, bool forking, bool out, bool err, int key)
{
	FILE *fout, *ferr;
	int pid, status, rc;
	log_debug("execute: %s %d %d", prog, out, err);
	if (forking) {
		ev_io *stderr_watcher = NULL;
		ev_io *stdout_watcher = NULL;
		pid = popen2_arr_p(NULL, &fout, &ferr, prog, args, NULL);

		if (err)
			stderr_watcher = add_io_watcher(t, ferr);
		else
			fclose(ferr);

		if (out)
			stdout_watcher = add_io_watcher(t, fout);
		else
			fclose(fout);

		add_child_watcher(t, pid, key, stdout_watcher, stderr_watcher);

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
		ui_redraw(&t->ui, REDRAW_FM);
		return status == 0;
	}
}


void print(const char *format, ...)
{
	va_list args;

	if (!_app)
		return;

	va_start(args, format);
	ui_vechom(&_app->ui, format, args);
	va_end(args);
}


void error(const char *format, ...)
{
	va_list args;

	if (!_app)
		return;

	va_start(args, format);
	ui_verror(&_app->ui, format, args);
	va_end(args);
}


void app_timeout_set(T *t, uint16_t duration)
{
	(void) t;
	input_timeout = current_millis() + duration;
}


void app_deinit(T *t)
{
	if (!t)
		return;

	for (size_t i = 0; i < cvector_size(child_watchers); i++) {
		struct child_watcher_data *data = child_watchers[i]->data;
		if (data->stdout_watcher) {
			free(data->stdout_watcher->data);
			free(data->stdout_watcher);
		}
		if (data->stderr_watcher) {
			free(data->stderr_watcher->data);
			free(data->stderr_watcher);
		}
		free(child_watchers[i]->data);
		free(child_watchers[i]);
	}
	notify_deinit();
	lua_deinit(t->L);
	ui_deinit(&t->ui);
	fm_deinit(&t->fm);
	tpool_wait(async_tm);
	tpool_destroy(async_tm);
	resultqueue_deinit(&async_results);
	if (fifo_fd > 0)
		close(fifo_fd);
	remove(cfg.fifopath);
}

#undef T
