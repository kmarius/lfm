#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <ncurses.h>
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "app.h"
#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "filter.h"
#include "log.h"
#include "ui.h"
#include "util.h"

#define T Ui

static void draw_dirs(T *t);
static void plane_draw_dir(struct ncplane *n, Dir *dir, char **sel,
		char **load, enum movemode_e mode, const char *highlight, bool print_sizes);
static void draw_cmdline(T *t);
static void draw_preview(T *t);
static void plane_draw_file_preview(struct ncplane *n, Preview *pv);
static void update_preview(T *t);
static void draw_menu(struct ncplane *n, cvector_vector_type(char *) menu);
static void draw_info(T *t);
static void menu_resize(T *t);
static char *ansi_consoom(struct ncplane *w, char *s);
static void ansi_addstr(struct ncplane *n, char *s);
static int print_shortened_w(struct ncplane *n, const wchar_t *name, int name_len, int max_len);

/* init/resize {{{ */

static int resize_cb(struct ncplane *n)
{
	/* TODO: dir->pos needs to be changed for all directories (on 2021-10-30) */
	T *ui = ncplane_userptr(n);
	notcurses_stddim_yx(ui->nc, &ui->nrow, &ui->ncol);
	log_debug("resize %d %d", ui->nrow, ui->ncol);
	ncplane_resize(ui->planes.info, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
	ncplane_resize(ui->planes.cmdline, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
	ncplane_move_yx(ui->planes.cmdline, ui->nrow - 1, 0);
	menu_resize(ui);
	ui_recol(ui);
	ui->fm->height = ui->nrow - 2;
	ui_clear(ui);
	return 0;
}


void ui_notcurses_init(T *t)
{
	struct notcurses_options ncopts = {
		.flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR,
	};
	t->nc = notcurses_core_init(&ncopts, NULL);
	if (!t->nc)
		exit(EXIT_FAILURE);

	struct ncplane *ncstd = notcurses_stdplane(t->nc);

	ncplane_dim_yx(ncstd, &t->nrow, &t->ncol);
	t->fm->height = t->nrow - 2;

	struct ncplane_options opts = {
		.y = 0,
		.x = 0,
		.rows = 1,
		.cols = t->ncol,
		.userptr = t,
	};

	opts.resizecb = resize_cb;
	t->planes.info = ncplane_create(ncstd, &opts);
	opts.resizecb = NULL;

	opts.y = t->nrow-1;
	t->planes.cmdline = ncplane_create(ncstd, &opts);

	ui_recol(t);

	opts.rows = opts.cols = 1;
	t->planes.menu = ncplane_create(ncstd, &opts);
	ncplane_move_bottom(t->planes.menu);
}


void ui_suspend(T *t)
{
	notcurses_stop(t->nc);
	t->nc = NULL;
	t->planes.dirs = NULL;
	t->planes.cmdline = NULL;
	t->planes.menu = NULL;
	t->planes.info = NULL;
	if (t->preview.preview) {
		cache_return(&t->preview.cache, t->preview.preview, t->preview.preview->path);
		t->preview.preview = NULL;
	}
}


void ui_init(T *t, Fm *fm)
{
	t->fm = fm;

	cache_init(&t->preview.cache, PREVIEW_CACHE_SIZE, (void(*)(void*)) preview_destroy);
	cmdline_init(&t->cmdline);
	history_load(&t->history, cfg.historypath);

	t->planes.dirs = NULL;
	t->planes.cmdline = NULL;
	t->planes.menu = NULL;
	t->planes.info = NULL;

	t->ndirs = 0;

	t->preview.preview = NULL;

	t->highlight = NULL;

	t->menubuf = NULL;
	t->message = false;

	ui_notcurses_init(t);

	log_info("initialized ui");
}


void ui_deinit(T *t)
{
	history_write(&t->history, cfg.historypath);
	history_deinit(&t->history);
	cvector_ffree(t->messages, free);
	cvector_ffree(t->menubuf, free);
	if (t->preview.preview) {
		cache_return(&t->preview.cache, t->preview.preview, t->preview.preview->path);
		t->preview.preview = NULL;
	}
	cache_deinit(&t->preview.cache);
	cmdline_deinit(&t->cmdline);
	cvector_ffree(t->planes.dirs, ncplane_destroy);
	ui_suspend(t);
}


void kbblocking(bool blocking)
{
	int val = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (val != -1)
		fcntl(STDIN_FILENO, F_SETFL, blocking ? val & ~O_NONBLOCK : val | O_NONBLOCK);
}


void ui_recol(T *t)
{
	struct ncplane *ncstd = notcurses_stdplane(t->nc);

	cvector_fclear(t->planes.dirs, ncplane_destroy);

	t->ndirs = cvector_size(cfg.ratios);

	uint16_t sum = 0;
	for (uint16_t i = 0; i < t->ndirs; i++)
		sum += cfg.ratios[i];

	struct ncplane_options opts = {
		.y = 1,
		.rows = t->nrow - 2,
	};

	uint16_t xpos = 0;
	for (uint16_t i = 0; i < t->ndirs - 1; i++) {
		opts.cols = (t->ncol - t->ndirs + 1) * cfg.ratios[i] / sum;
		opts.x = xpos;
		cvector_push_back(t->planes.dirs, ncplane_create(ncstd, &opts));
		xpos += opts.cols + 1;
	}
	opts.x = xpos;
	opts.cols = t->ncol - xpos - 1;
	cvector_push_back(t->planes.dirs, ncplane_create(ncstd, &opts));
	t->planes.preview = t->planes.dirs[t->ndirs-1];
}


/* }}} */

/* main drawing/echo/err {{{ */

static inline void ui_redraw(T *t, uint8_t mode);


void ui_draw(T *t)
{
	if (t->redraw & REDRAW_FM)
		draw_dirs(t);
	if (t->redraw & (REDRAW_MENU | REDRAW_MENU))
		draw_menu(t->planes.menu, t->menubuf);
	if (t->redraw & (REDRAW_FM | REDRAW_CMDLINE))
		draw_cmdline(t);
	if (t->redraw & (REDRAW_FM | REDRAW_INFO))
		draw_info(t);
	if (t->redraw & (REDRAW_FM | REDRAW_PREVIEW))
		draw_preview(t);
	if (t->redraw) {
		notcurses_render(t->nc);
	}
	t->redraw = 0;
}


void ui_clear(T *t)
{
	/* infoline and dirs have to be cleared *and* rendered, otherwise they will
	 * bleed into the first row */
	ncplane_erase(notcurses_stdplane(t->nc));
	ncplane_erase(t->planes.info);
	for (uint16_t i = 0; i < t->ndirs; i++)
		ncplane_erase(t->planes.dirs[i]);

	ncplane_erase(t->planes.cmdline);

	notcurses_render(t->nc);

	notcurses_refresh(t->nc, NULL, NULL);

	notcurses_cursor_enable(t->nc, 0, 0);
	notcurses_cursor_disable(t->nc);

	ui_redraw(t, REDRAW_FM);
}


static void draw_dirs(T *t)
{
	const uint16_t l = t->fm->dirs.length;
	for (uint16_t i = 0; i < l; i++) {
		plane_draw_dir(t->planes.dirs[l-i-1],
				t->fm->dirs.visible[i],
				t->fm->selection.files,
				t->fm->load.files,
				t->fm->load.mode,
				i == 0 ? t->highlight : NULL, i == 0);
	}
}


static void draw_preview(T *t)
{
	if (cfg.preview && t->ndirs > 1) {
		if (t->fm->dirs.preview) {
			plane_draw_dir(t->planes.preview, t->fm->dirs.preview, t->fm->selection.files,
					t->fm->load.files, t->fm->load.mode, NULL, false);
		} else {
			update_preview(t);
			plane_draw_file_preview(t->planes.preview, t->preview.preview);
		}
	}
}


void ui_echom(T *t, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	ui_vechom(t, format, args);
	va_end(args);
}


void ui_error(T *t, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	ui_verror(t, format, args);
	va_end(args);
}


void ui_verror(T *t, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	log_error(msg);

	cvector_push_back(t->messages, msg);

	/* TODO: show messages after initialization (on 2021-10-30) */
	if (!t->nc)
		return;

	ncplane_erase(t->planes.cmdline);
	ncplane_set_fg_palindex(t->planes.cmdline, COLOR_RED);
	ncplane_putstr_yx(t->planes.cmdline, 0, 0, msg);
	ncplane_set_fg_default(t->planes.cmdline);
	notcurses_render(t->nc);
	t->message = true;
}


void ui_vechom(T *t, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	cvector_push_back(t->messages, msg);

	if (!t->nc)
		return;

	ncplane_erase(t->planes.cmdline);
	ncplane_set_fg_palindex(t->planes.cmdline, 15);
	ncplane_putstr_yx(t->planes.cmdline, 0, 0, msg);
	ncplane_set_fg_default(t->planes.cmdline);
	notcurses_render(t->nc);
	t->message = true;

}


/* }}} */

/* cmd line {{{ */


void ui_cmd_prefix_set(T *t, const char *prefix)
{
	if (!prefix)
		return;

	t->message = false;
	notcurses_cursor_enable(t->nc, 0, 0);
	cmdline_prefix_set(&t->cmdline, prefix);
	ui_redraw(t, REDRAW_CMDLINE);
}


void ui_cmd_clear(T *t)
{
	cmdline_clear(&t->cmdline);
	history_reset(&t->history);
	notcurses_cursor_disable(t->nc);
	ui_showmenu(t, NULL);
	ui_redraw(t, REDRAW_CMDLINE);
	ui_redraw(t, REDRAW_MENU);
}


static char *print_time(time_t time, char *buffer, size_t bufsz)
{
	strftime(buffer, bufsz, "%Y-%m-%d %H:%M:%S", localtime(&time));
	return buffer;
}


static uint16_t int_sz(uint16_t n)
{
	uint16_t i = 1;
	while (n >= 10) {
		i++;
		n /= 10;
	}
	return i;
}


void draw_cmdline(T *t)
{
	char nums[16];
	char size[32];
	char mtime[32];

	if (t->message)
		return;

	ncplane_erase(t->planes.cmdline);
	/* sometimes the color is changed to grey */
	ncplane_set_bg_default(t->planes.cmdline);
	ncplane_set_fg_default(t->planes.cmdline);

	uint16_t rhs_sz = 0;
	uint16_t lhs_sz = 0;

	if (!cmdline_prefix_get(&t->cmdline)) {
		const Dir *dir = t->fm->dirs.visible[0];
		if (dir) {
			const File *file = dir_current_file(dir);
			if (file) {
				/* TODO: for empty directories, show the stat of the
				 * directory instead? (on 2021-07-18) */
				lhs_sz = ncplane_printf_yx(t->planes.cmdline, 0, 0,
						"%s %2.ld %s %s %4s %s%s%s",
						file_perms(file), file_nlink(file),
						file_owner(file), file_group(file),
						file_size_readable(file, size),
						print_time(file_mtime(file), mtime, sizeof(mtime)),
						file_islink(file) ? " -> " : "",
						file_islink(file) ? file_link_target(file) : "");
			}

			rhs_sz = snprintf(nums, sizeof(nums), "%u/%u", dir->length > 0 ? dir->ind + 1 : 0, dir->length);
			ncplane_putstr_yx(t->planes.cmdline, 0, t->ncol - rhs_sz, nums);

			// these are drawn right to left
			if (dir->filter) {
				rhs_sz += mbstowcs(NULL, filter_string(dir->filter), 0) + 2 + 1;
				ncplane_set_bg_palindex(t->planes.cmdline, COLOR_GREEN);
				ncplane_set_fg_palindex(t->planes.cmdline, COLOR_BLACK);
				ncplane_putchar_yx(t->planes.cmdline, 0, t->ncol - rhs_sz, ' ');
				ncplane_putstr(t->planes.cmdline, filter_string(dir->filter));
				ncplane_putchar(t->planes.cmdline, ' ');
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_set_fg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (cvector_size(t->fm->load.files) > 0) {
				if (t->fm->load.mode == MODE_COPY)
					ncplane_set_channels(t->planes.cmdline, cfg.colors.copy);
				else
					ncplane_set_channels(t->planes.cmdline, cfg.colors.delete);

				rhs_sz += int_sz(cvector_size(t->fm->load.files)) + 2 + 1;
				ncplane_printf_yx(t->planes.cmdline, 0, t->ncol-rhs_sz, " %lu ", cvector_size(t->fm->load.files));
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_set_fg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (t->fm->selection.length > 0) {
				ncplane_set_channels(t->planes.cmdline, cfg.colors.selection);
				rhs_sz += int_sz(t->fm->selection.length) + 2 + 1;
				ncplane_printf_yx(t->planes.cmdline, 0, t->ncol - rhs_sz, " %d ", t->fm->selection.length);
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_set_fg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (t->keyseq) {
				char *str = NULL;
				for (size_t i = 0; i < cvector_size(t->keyseq); i++) {
					for (const char *s = input_to_key_name(t->keyseq[i]); *s; s++)
						cvector_push_back(str, *s);
				}
				cvector_push_back(str, 0);
				rhs_sz += mbstowcs(NULL, str, 0) + 1;
				ncplane_putstr_yx(t->planes.cmdline, 0, t->ncol - rhs_sz, str);
				ncplane_putchar(t->planes.cmdline, ' ');
				cvector_free(str);
			}
			if (lhs_sz + rhs_sz > t->ncol) {
				ncplane_putwc_yx(t->planes.cmdline, 0, t->ncol - rhs_sz - 2, cfg.truncatechar);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
		}
	} else {
		const uint16_t cursor_pos = cmdline_print(&t->cmdline, t->planes.cmdline);
		notcurses_cursor_enable(t->nc, t->nrow - 1, cursor_pos);
	}
}

/* }}} */

/* info line {{{ */

static void draw_info(T *t)
{
	// arbitrary
	static char user[32] = {0};
	static char host[HOST_NAME_MAX + 1] = {0};
	static char *home;
	static uint16_t home_len;

	if (user[0] == 0) {
		strncpy(user, getenv("USER"), sizeof(user) - 1);
		gethostname(host, sizeof(host));
		home = getenv("HOME");
		home_len = mbstowcs(NULL, home, 0);
	}

	struct ncplane *n = t->planes.info;

	ncplane_erase(n);

	ncplane_set_styles(n, NCSTYLE_BOLD);
	ncplane_set_fg_palindex(n, COLOR_GREEN);
	ncplane_putstr_yx(n, 0, 0, user);
	ncplane_putchar(n, '@');
	ncplane_putstr(n, host);
	ncplane_set_fg_default(n);

	ncplane_set_styles(n, NCSTYLE_NONE);
	ncplane_putchar(n, ':');
	ncplane_set_styles(n, NCSTYLE_BOLD);

	const Dir *dir = fm_current_dir(t->fm);
	const File *file = dir_current_file(dir);
	int path_len, name_len;
	wchar_t *path_ = ambstowcs(dir->path, &path_len);
	wchar_t *path = path_;
	wchar_t *name = NULL;

	// shortening should work fine with ascii only names
	wchar_t *end = (wchar_t *) wcsend(path);
	int remaining;
	ncplane_cursor_yx(n, NULL, &remaining);
	remaining = t->ncol - remaining;
	if (file) {
		name = ambstowcs(file_name(file), &name_len);
		remaining -= name_len;
	}
	ncplane_set_fg_palindex(n, COLOR_BLUE);
	if (home && hasprefix(dir->path, home)) {
		ncplane_putchar(n, '~');
		remaining--;
		path += home_len;
	}

	if (!dir_isroot(dir))
		remaining--; // printing another '/' later

	// shorten path components if necessary
	while (*path && end - path > remaining) {
		ncplane_putchar(n, '/');
		remaining--;
		wchar_t *next = wcschr(++path, '/');
		if (!next)
			next = end;

		if (end - next <= remaining) {
			// Everything after the next component fits, we can print some of this one
			const int m = remaining - (end - next) - 1;
			if (m >= 2) {
				wchar_t *print_end = path + m;
				remaining -= m;
				while (path < print_end)
					ncplane_putwc(n, *(path++));

				if (*path != '/') {
					ncplane_putwc(n, cfg.truncatechar);
					remaining--;
				}
			} else {
				ncplane_putwc(n, *path);
				remaining--;
				path = next;
			}
			path = next;
		} else {
			// print one char only.
			ncplane_putwc(n, *path);
			remaining--;
			path = next;
		}
	}
	ncplane_putwstr(n, path);

	if (!dir_isroot(dir))
		ncplane_putchar(n, '/');

	if (file) {
		ncplane_cursor_yx(n, NULL, &remaining);
		remaining = t->ncol - remaining;
		ncplane_set_fg_default(n);
		print_shortened_w(n, name, name_len, remaining);
	}

	free(path_);
	free(name);
}

/* }}} */

/* menu {{{ */

static void ansi_addstr(struct ncplane *n, char *s)
{
	while (*s) {
		if (*s == '\033') {
			s = ansi_consoom(n, s);
		} else {
			char *c;
			for (c = s; *s != 0 && *s != '\033'; s++);
			if (ncplane_putnstr(n, s-c, c) == -1)
				return; // EOL
		}
	}
}


static void draw_menu(struct ncplane *n, cvector_vector_type(char *) menubuf)
{
	if (!menubuf)
		return;

	ncplane_erase(n);

	/* otherwise this doesn't draw over the directories */
	/* Still needed as of 2022-01-16 */
	ncplane_set_base(n, " ", 0, 0);

	for (size_t i = 0; i < cvector_size(menubuf); i++) {
		ncplane_cursor_move_yx(n, i, 0);
		const char *s = menubuf[i];
		uint16_t xpos = 0;

		while (*s != 0) {
			while (*s != 0 && *s != '\t') {
				ncplane_putchar(n, *(s++));
				xpos++;
			}
			if (*s == '\t') {
				ncplane_putchar(n, ' ');
				s++;
				xpos++;
				for (const uint16_t l = ((xpos/8)+1)*8; xpos < l; xpos++)
					ncplane_putchar(n, ' ');
			}
		}
	}
}


static void menu_resize(T *t)
{
	/* TODO: find out why, after resizing, the menu is behind the dirs (on 2021-10-30) */
	const uint16_t h = max(1, min(cvector_size(t->menubuf), t->nrow - 2));
	ncplane_resize(t->planes.menu, 0, 0, 0, 0, 0, 0, h, t->ncol);
	ncplane_move_yx(t->planes.menu, t->nrow - 1 - h, 0);
}


static void menu_clear(T *t)
{
	if (!t->menubuf)
		return;

	ncplane_erase(t->planes.menu);
	ncplane_move_bottom(t->planes.menu);
}


void ui_showmenu(T *t, cvector_vector_type(char*) vec)
{
	if (t->menubuf) {
		menu_clear(t);
		cvector_ffree(t->menubuf, free);
		t->menubuf = NULL;
	}
	if (cvector_size(vec) > 0) {
		t->menubuf = vec;
		menu_resize(t);
		ncplane_move_top(t->planes.menu);
	}
	ui_redraw(t, REDRAW_MENU);
}

void ui_show_keyseq(Ui *ui, input_t *keyseq);

/* }}} */

/* draw_dir {{{ */

static uint64_t ext_channel_find(const char *ext)
{
	if (ext) {
		for (size_t i = 0; i < cvector_size(cfg.colors.ext_channels); i++) {
			if (strcaseeq(ext, cfg.colors.ext_channels[i].ext))
				return cfg.colors.ext_channels[i].channel;
		}
	}
	return 0;
}


/* TODO: we shouldn't shorten extensions on directories (on 2022-02-23) */
static int print_shortened_w(struct ncplane *n, const wchar_t *name, int name_len, int max_len)
{
	if (max_len <= 0)
		return 0;

	const wchar_t *ext = wcsrchr(name, L'.');

	if (!ext || ext == name)
		ext = name + name_len;
	const int ext_len = name_len - (ext - name);

	int x = max_len;
	if (name_len <= max_len) {
		// everything fits
		x = ncplane_putwstr(n, name);
	} else if (max_len > ext_len + 1) {
		// print extension and as much of the name as possible
		int print_name_ind = max_len - ext_len - 1;
		const wchar_t *print_name_ptr = name + print_name_ind;
		while (name < print_name_ptr)
			ncplane_putwc(n, *(name++));
		ncplane_putwc(n, cfg.truncatechar);
		ncplane_putwstr(n, ext);
	} else if (max_len >= 5) {
		// print first char of the name and as mutch of the extension as possible
		ncplane_putwc(n, *(name));
		const wchar_t *ext_end = ext + max_len - 2 - 1;
		ncplane_putwc(n, cfg.truncatechar);
		while (ext < ext_end)
			ncplane_putwc(n, *(ext++));
		ncplane_putwc(n, cfg.truncatechar);
	} else if (max_len > 1) {
		const wchar_t *name_end = name + max_len - 1;
		while (name < name_end)
			ncplane_putwc(n, *(name++));
		ncplane_putwc(n, cfg.truncatechar);
	} else {
		ncplane_putwc(n, *name);
	}

	return x;
}

static inline int print_shortened(struct ncplane *n, const char *name, int max_len)
{
	if (max_len <= 0)
		return 0;

	int name_len;
	wchar_t *namew = ambstowcs(name, &name_len);
	int ret = print_shortened_w(n, namew, name_len, max_len);
	free(namew);
	return ret;
}


static int print_highlighted_and_shortened(struct ncplane *n, const char *name, const char *hl, int max_len)
{
	if (max_len <= 0)
		return 0;

	int name_len, hl_len;
	wchar_t *namew_ = ambstowcs(name, &name_len);
	wchar_t *hlw = ambstowcs(hl, &hl_len);
	wchar_t *extw = wcsrchr(namew_, L'.');
	if (!extw || extw == namew_)
		extw = namew_ + name_len;
	int ext_len = name_len - (extw - namew_);
	const wchar_t *hl_begin = wstrcasestr(namew_, hlw);
	const wchar_t *hl_end = hl_begin + hl_len;

	const uint64_t ch = ncplane_channels(n);
	int x = max_len;
	wchar_t *namew = namew_;

	/* TODO: some of these branches can probably be optimized/combined (on 2022-02-18) */
	if (name_len <= max_len) {
		// everything fits
		while (namew < hl_begin)
			ncplane_putwc(n, *(namew++));
		ncplane_set_channels(n, cfg.colors.search);
		while (namew < hl_end)
			ncplane_putwc(n, *(namew++));
		ncplane_set_channels(n, ch);
		while (*namew)
			ncplane_putwc(n, *(namew++));
		x = name_len;
	} else if (max_len > ext_len + 1) {
		// print extension and as much of the name as possible
		wchar_t *print_name_end = namew + max_len - ext_len - 1;
		if (hl_begin < print_name_end) {
			// highlight begins before truncate
			while (namew < hl_begin)
				ncplane_putwc(n, *(namew++));
			ncplane_set_channels(n, cfg.colors.search);
			if (hl_end < print_name_end) {
				// highlight ends before truncate
				while (namew < hl_end)
					ncplane_putwc(n, *(namew++));
				ncplane_set_channels(n, ch);
				while (namew < print_name_end)
					ncplane_putwc(n, *(namew++));
			} else {
				// highlight continues during truncate
				while (namew < print_name_end)
					ncplane_putwc(n, *(namew++));
			}
			ncplane_putwc(n, cfg.truncatechar);
		} else {
			// highlight begins after truncate
			while (namew < print_name_end)
				ncplane_putwc(n, *(namew++));
			if (hl_begin < extw) {
				// highlight begins before extension begins
				ncplane_set_channels(n, cfg.colors.search);
			}
			ncplane_putwc(n, cfg.truncatechar);
		}
		if (hl_begin >= extw) {
			while (extw < hl_begin)
				ncplane_putwc(n, *(extw++));
			ncplane_set_channels(n, cfg.colors.search);
			while (extw < hl_end)
				ncplane_putwc(n, *(extw++));
			ncplane_set_channels(n, ch);
			while (*extw)
				ncplane_putwc(n, *(extw++));
		} else {
			// highlight was started before
			while (extw < hl_end)
				ncplane_putwc(n, *(extw++));
			ncplane_set_channels(n, ch);
			while (*extw)
				ncplane_putwc(n, *(extw++));
		}
	} else if (max_len >= 5) {
		const wchar_t *ext_end = extw + max_len - 2 - 1;
		if (hl_begin == namew_) {
			ncplane_set_channels(n, cfg.colors.search);
		}
		ncplane_putwc(n, *name);
		if (hl_begin < extw) {
			ncplane_set_channels(n, cfg.colors.search);
		}
		ncplane_putwc(n, cfg.truncatechar);
		if (hl_end <= extw) {
			ncplane_set_channels(n, ch);
		}
		if (hl_begin >= extw) {
			while (extw < hl_begin)
				ncplane_putwc(n, *(extw++));
			ncplane_set_channels(n, cfg.colors.search);
			if (hl_end < ext_end) {
				while (extw < hl_end)
					ncplane_putwc(n, *(extw++));
				ncplane_set_channels(n, ch);
			}
			while (extw < ext_end)
				ncplane_putwc(n, *(extw++));
			ncplane_putwc(n, cfg.truncatechar);
			ncplane_set_channels(n, ch);
		} else {
			while (extw < ext_end)
				ncplane_putwc(n, *(extw++));
			ncplane_putwc(n, cfg.truncatechar);
		}
	} else if (max_len > 1) {
		const wchar_t *name_end = namew_ + max_len - 1;
		if (hl_begin < name_end) {
			while (namew < hl_begin)
				ncplane_putwc(n, *(namew++));
			ncplane_set_channels(n, cfg.colors.search);
			if (hl_end < name_end) {
				while (namew < hl_end)
					ncplane_putwc(n, *(namew++));
				ncplane_set_channels(n, ch);
				while (namew < name_end)
					ncplane_putwc(n, *(namew++));
			} else {
				while (namew < name_end)
					ncplane_putwc(n, *(namew++));
			}
		} else {
			while (namew < name_end)
				ncplane_putwc(n, *(namew++));
			ncplane_set_channels(n, cfg.colors.search);
		}
		ncplane_putwc(n, cfg.truncatechar);
		ncplane_set_channels(n, ch);
	} else {
		// only one char
		if (hl == name) {
			const uint64_t ch = ncplane_channels(n);
			ncplane_set_channels(n, cfg.colors.search);
			ncplane_putwc(n, *namew_);
			ncplane_set_channels(n, ch);
		} else {
			ncplane_putwc(n, *namew_);
		}
	}

	free(hlw);
	free(namew_);
	return x;
}


static void print_file(struct ncplane *n, const File *file,
		bool iscurrent, char **sel, char **load, enum movemode_e mode,
		const char *highlight, bool print_sizes)
{
	int ncol, y0;
	int x = 0;
	char size[16];
	ncplane_dim_yx(n, NULL, &ncol);
	ncplane_cursor_yx(n, &y0, NULL);

	int rightmargin = 0;

	if (print_sizes) {
		if (file_isdir(file)) {
			if (file_dircount(file) < 0)
				snprintf(size, sizeof(size), "?");
			else
				snprintf(size, sizeof(size), "%d", file_dircount(file));
		} else {
			file_size_readable(file, size);
		}
		rightmargin = strlen(size) + 1;

		if (file_islink(file))
			rightmargin += 3; /* " ->" */
		if (file_ext(file)) {
			if (ncol - 3 - rightmargin < 4)
				rightmargin = 0;
		} else {
			if (ncol - 3 - rightmargin < 2)
				rightmargin = 0;
		}
	}

	ncplane_set_bg_default(n);

	if (cvector_contains_str(sel, file_path(file)))
		ncplane_set_channels(n, cfg.colors.selection);
	else if (mode == MODE_MOVE && cvector_contains_str(load, file_path(file)))
		ncplane_set_channels(n, cfg.colors.delete);
	else if (mode == MODE_COPY && cvector_contains_str(load, file_path(file)))
		ncplane_set_channels(n, cfg.colors.copy);

	// this is needed because when selecting with space the filename is printed
	// as black (bug in notcurses)
	// 2021-08-21
	ncplane_set_fg_default(n);

	ncplane_putchar(n, ' ');
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);

	if (file_isdir(file)) {
		ncplane_set_channels(n, cfg.colors.dir);
		ncplane_set_styles(n, NCSTYLE_BOLD);
	} else if (file_isbroken(file)) {
		ncplane_set_channels(n, cfg.colors.broken);
	} else if (file_isexec(file)) {
		ncplane_set_channels(n, cfg.colors.exec);
	} else {
		uint64_t ch = ext_channel_find(file_ext(file));
		if (ch > 0) {
			ncplane_set_channels(n, ch);
		} else {
			ncplane_set_channels(n, cfg.colors.normal);
			/* ncplane_set_fg_default(n); */
		}
	}

	if (iscurrent)
		ncplane_set_bchannel(n, cfg.colors.current);

	ncplane_putchar(n, ' ');

	const char *hlsubstr = highlight && highlight[0] ? strcasestr(file_name(file), highlight) : NULL;
	const int left_space = ncol - 3 - rightmargin;
	if (hlsubstr) {
		x += print_highlighted_and_shortened(n, file_name(file), highlight, left_space);
	}
	else {
		x += print_shortened(n, file_name(file), left_space);
	}

	for (; x < ncol - rightmargin - 1; x++)
		ncplane_putchar(n, ' ');

	if (rightmargin > 0) {
		ncplane_cursor_move_yx(n, y0, ncol - rightmargin);
		if (file_islink(file))
			ncplane_putstr(n, "-> ");
		ncplane_putstr(n, size);
		ncplane_putchar(n, ' ');
	}
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);
	ncplane_set_styles(n, NCSTYLE_NONE);
}


static void plane_draw_dir(struct ncplane *n, Dir *dir, char **sel, char **load,
		enum movemode_e mode, const char *highlight, bool print_sizes)
{
	int nrow, i, offset;

	ncplane_erase(n);
	ncplane_dim_yx(n, &nrow, NULL);
	ncplane_cursor_move_yx(n, 0, 0);

	if (!dir)
		return;

	if (dir->error) {
		ncplane_putstr_yx(n, 0, 2, strerror(dir->error));
	} else if (dir_loading(dir)) {
		ncplane_putstr_yx(n, 0, 2, "loading");
	} else if (dir->length == 0) {
		if (dir->length_all > 0) {
			ncplane_putstr_yx(n, 0, 2, "contains hidden files");
		} else {
			ncplane_putstr_yx(n, 0, 2, "empty");
		}
	} else {
		dir->pos = min(min(dir->pos, nrow - 1), dir->ind);

		offset = max(dir->ind - dir->pos, 0);

		if (dir->length <= (uint32_t) nrow)
			offset = 0;

		const int l = min(dir->length - offset, nrow);
		for (i = 0; i < l; i++) {
			ncplane_cursor_move_yx(n, i, 0);
			print_file(n, dir->files[i + offset],
					i == dir->pos, sel, load, mode, highlight, print_sizes);
		}
	}
}
/* }}} */

/* preview {{{ */

static Preview *load_preview(T *t, File *file)
{
	int ncol, nrow;
	ncplane_dim_yx(t->planes.preview, &nrow, &ncol);

	Preview *pv = cache_take(&t->preview.cache, file_path(file));
	if (pv) {
		/* TODO: vv (on 2021-08-10) */
		/* might be checking too often here? or is it capped by inotify
		 * timeout? */
		if (pv->nrow < t->nrow - 2) {
			async_preview_load(pv, nrow);
			pv->loading = true;
		} else {
			async_preview_check(pv);
		}
	} else {
		pv = preview_create_loading(file_path(file), nrow);
		async_preview_load(pv, nrow);
	}
	return pv;
}


static void update_preview(T *t)
{
	int ncol, nrow;
	ncplane_dim_yx(t->planes.preview, &nrow, &ncol);

	File *file = fm_current_file(t->fm);
	if (file && !file_isdir(file)) {
		if (t->preview.preview) {
			if (streq(t->preview.preview->path, file_path(file))) {
				if (!t->preview.preview->loading) {
					if (t->preview.preview->nrow < nrow) {
						async_preview_load(t->preview.preview, nrow);
						t->preview.preview->loading = true;
					} else {
						async_preview_check(t->preview.preview);
					}
				}
			} else {
				cache_return(&t->preview.cache, t->preview.preview, t->preview.preview->path);
				t->preview.preview = load_preview(t, file);
				ui_redraw(t, REDRAW_PREVIEW);
			}
		} else {
			t->preview.preview = load_preview(t, file);
			ui_redraw(t, REDRAW_PREVIEW);
		}
	} else {
		if (t->preview.preview) {
			cache_return(&t->preview.cache, t->preview.preview, t->preview.preview->path);
			t->preview.preview = NULL;
			ui_redraw(t, REDRAW_PREVIEW);
		}
	}
}


static void wansi_matchattr(struct ncplane *w, uint16_t a)
{
	if (a <= 9) {
		switch (a) {
			case 0:
				ncplane_set_styles(w, NCSTYLE_NONE);
				ncplane_set_fg_default(w);
				ncplane_set_bg_default(w);
				break;
			case 1:
				ncplane_on_styles(w, NCSTYLE_BOLD);
				break;
			case 2:
				/* not supported by notcurses */
				ncplane_on_styles(w, WA_DIM);
				break;
			case 3:
				ncplane_on_styles(w, NCSTYLE_ITALIC);
				break;
			case 4:
				ncplane_on_styles(w, NCSTYLE_UNDERLINE);
				break;
			case 5:
				/* not supported by notcurses */
				ncplane_on_styles(w, NCSTYLE_BLINK);
				break;
			case 6: /* nothing */
				break;
			case 7:
				/* not supported, needs workaround */
				ncplane_on_styles(w, NCSTYLE_REVERSE);
				break;
			case 8:
				/* not supported by notcurses */
				ncplane_on_styles(w, NCSTYLE_INVIS);
				break;
			case 9: /* strikethrough */
				ncplane_on_styles(w, NCSTYLE_STRUCK);
				break;
			default:
				break;
		}
	} else if (a >= 30 && a <= 37) {
		ncplane_set_fg_palindex(w, a-30);
	} else if (a >= 40 && a <= 47) {
		ncplane_set_bg_palindex(w, a-40);
	}
}


/*
 * Consooms ansi color escape sequences and sets ATTRS
 * should be called with a pointer at \033
 */
static char *ansi_consoom(struct ncplane *w, char *s)
{
	char c;
	uint16_t acc = 0;
	uint16_t nnums = 0;
	uint16_t nums[6];
	s++; // first char guaranteed to be \033
	if (*s != '[') {
		log_error("there should be a [ here");
		return s;
	}
	s++;
	bool fin = false;
	while (!fin) {
		switch (c = *s) {
			case 'm':
				nums[nnums++] = acc;
				acc = 0;
				fin = true;
				break;
			case ';':
				nums[nnums++] = acc;
				acc = 0;
				break;
			case '\0':
				log_error("escape ended prematurely");
				return s;
			default:
				if (!(c >= '0' && c <= '9')) {
					log_error("not a number? %c", c);
				}
				acc = 10 * acc + (c - '0');
		}
		if (nnums > 5) {
			log_error("malformed ansi: too many numbers");
			/* TODO: seek to 'm' (on 2021-07-29) */
			return s;
		}
		s++;
	}
	if (nnums == 0) {
		/* highlight actually does this, but it will be recognized as \e[0m instead */
	} else if (nnums == 1) {
		wansi_matchattr(w, nums[0]);
	} else if (nnums == 2) {
		wansi_matchattr(w, nums[0]);
		wansi_matchattr(w, nums[1]);
	} else if (nnums == 3) {
		if (nums[0] == 38 && nums[1] == 5)
			ncplane_set_fg_palindex(w, nums[2]);
		else if (nums[0] == 48 && nums[1] == 5)
			log_error("trying to set background color per ansi code");
	} else if (nnums == 4) {
		wansi_matchattr(w, nums[0]);
		ncplane_set_fg_palindex(w, nums[3]);
	} else if (nnums == 5) {
		log_error("using unsupported ansi code with 5 numbers");
		/* log_debug("%d %d %d %d %d", nums[0], nums[1], nums[2],
		 * nums[3], nums[4]);
		 */
	} else if (nnums == 6) {
		log_error("using unsupported ansi code with 6 numbers");
		/* log_debug("%d %d %d %d %d %d", nums[0], nums[1], nums[2],
		 * nums[3], nums[4], nums[5]); */
	}

	return s;
}


static void plane_draw_file_preview(struct ncplane *n, Preview *pv)
{
	ncplane_erase(n);

	if (!pv)
		return;

	int nrow;
	ncplane_dim_yx(n, &nrow, NULL);
	ncplane_set_styles(n, NCSTYLE_NONE);
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);

	for (size_t i = 0; i < cvector_size(pv->lines) && i < (size_t) nrow; i++) {
		ncplane_cursor_move_yx(n, i, 0);
		ansi_addstr(n, pv->lines[i]);
	}
}


void ui_drop_cache(T *t)
{
	if (t->preview.preview) {
		cache_return(&t->preview.cache, t->preview.preview, t->preview.preview->path);
		t->preview.preview = NULL;
	}
	cache_drop(&t->preview.cache);
	update_preview(t);
	ui_redraw(t, REDRAW_CMDLINE);
	ui_redraw(t, REDRAW_PREVIEW);
}

/* }}} */

#undef T
