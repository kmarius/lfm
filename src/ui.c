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
				 * directory instead (on 2021-07-18) */
				lhs_sz = ncplane_printf_yx(t->planes.cmdline, 0, 0,
						"%s %2.ld %s %s %4s %s%s%s",
						file_perms(file), file_nlink(file),
						file_owner(file), file_group(file),
						file_size_readable(file, size),
						print_time(file_mtime(file), mtime, sizeof(mtime)),
						file_islink(file) ? " -> " : "",
						file_islink(file) ? file_link_target(file) : "");
			}

			rhs_sz = snprintf(nums, sizeof(nums), " %d/%d", dir->length ? dir->ind + 1 : 0, dir->length);
			ncplane_putstr_yx(t->planes.cmdline, 0, t->ncol - rhs_sz, nums);

			if (dir->filter[0] != 0) {
				rhs_sz += strlen(dir->filter) + 3;
				ncplane_set_bg_palindex(t->planes.cmdline, COLOR_GREEN);
				ncplane_set_fg_palindex(t->planes.cmdline, COLOR_BLACK);
				ncplane_printf_yx(t->planes.cmdline, 0, t->ncol-rhs_sz+1, " %s ", dir->filter);
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_set_fg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (cvector_size(t->fm->load.files) > 0) {
				if (t->fm->load.mode == MODE_COPY)
					ncplane_set_channels(t->planes.cmdline, cfg.colors.copy);
				else
					ncplane_set_channels(t->planes.cmdline, cfg.colors.delete);

				rhs_sz += int_sz(cvector_size(t->fm->load.files)) + 3;
				ncplane_printf_yx(t->planes.cmdline, 0, t->ncol-rhs_sz+1, " %lu ", cvector_size(t->fm->load.files));
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (t->fm->selection.length > 0) {
				ncplane_set_channels(t->planes.cmdline, cfg.colors.selection);
				rhs_sz += int_sz(t->fm->selection.length) + 3;
				ncplane_printf_yx(t->planes.cmdline, 0, t->ncol-rhs_sz+1, " %d ", t->fm->selection.length);
				ncplane_set_bg_default(t->planes.cmdline);
				ncplane_putchar(t->planes.cmdline, ' ');
			}
			if (lhs_sz + rhs_sz > t->ncol) {
				ncplane_putwc_yx(t->planes.cmdline, 0, t->ncol - rhs_sz - 1, cfg.truncatechar);
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

	ncplane_erase(t->planes.info);

	if (user[0] == 0) {
		getlogin_r(user, sizeof(user));
		gethostname(host, sizeof(host));
		home = getenv("HOME");
		home_len = strlen(home);
	}

	ncplane_set_styles(t->planes.info, NCSTYLE_BOLD);
	ncplane_set_fg_palindex(t->planes.info, COLOR_GREEN);
	ncplane_putstr_yx(t->planes.info, 0, 0, user);
	ncplane_putchar(t->planes.info, '@');
	ncplane_putstr(t->planes.info, host);
	ncplane_set_fg_default(t->planes.info);

	ncplane_set_styles(t->planes.info, NCSTYLE_NONE);
	ncplane_putchar(t->planes.info, ':');
	ncplane_set_styles(t->planes.info, NCSTYLE_BOLD);


	const Dir *dir = t->fm->dirs.visible[0];
	if (dir) {
		// shortening should work fine with ascii only names
		const char *end = dir->path + strlen(dir->path);
		int remaining;
		ncplane_cursor_yx(t->planes.info, NULL, &remaining);
		remaining = t->ncol - remaining;
		const File *file = dir_current_file(dir);
		if (file)
			remaining -= strlen(file_name(file));
		ncplane_set_fg_palindex(t->planes.info, COLOR_BLUE);
		const char *c = dir->path;
		if (home && hasprefix(dir->path, home)) {
			ncplane_putchar(t->planes.info, '~');
			remaining--;
			c += home_len;
		}
		while (*c && end - c > remaining) {
			ncplane_putchar(t->planes.info, '/');
			ncplane_putchar(t->planes.info, *(++c));
			remaining -= 2;
			while (*(++c) && (*c != '/'))
				;
		}
		ncplane_putstr(t->planes.info, c);
		if (!dir_isroot(dir))
			ncplane_putchar(t->planes.info, '/');
		if (file) {
			ncplane_set_fg_default(t->planes.info);
			ncplane_putstr(t->planes.info, file_name(file));
		}
	}
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
		rightmargin = strlen(size) + 2;

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

	const char *hlsubstr;
	ncplane_putchar(n, ' ');
	if (highlight && (hlsubstr = strcasestr(file_name(file), highlight))) {
		/* TODO: how can we combine this with shortening? Try to show ...highlight... ? (on 2022-02-07) */
		const uint16_t l = hlsubstr - file_name(file);
		const uint64_t ch = ncplane_channels(n);
		ncplane_putnstr(n, l, file_name(file));
		ncplane_set_channels(n, cfg.colors.search);
		ncplane_putnstr(n, strlen(highlight), file_name(file) + l);
		ncplane_set_channels(n, ch);
		ncplane_putnstr(n, ncol - 3, file_name(file) + l + strlen(highlight));
		ncplane_cursor_yx(n, NULL, &x);
	} else {
		// TODO: Calculations should be done with the mblen of the strings (on 2022-02-07)
		if (file_ext(file)) {
			const int name_len = strlen(file_name(file));
			const int ext_len = strlen(file_ext(file));
			const int pre_len = name_len - ext_len;
			const int left_space = ncol - 3 - rightmargin;
			if (left_space >= ext_len + pre_len) {
				// print whole
				x += ncplane_putnstr(n, left_space, file_name(file));
			} else if (left_space >= ext_len + 2) {
				// shorten prefix, print extension
				x += ncplane_putnstr(n, left_space - ext_len - 1, file_name(file));
				x += ncplane_putwc(n, cfg.truncatechar);
				x += ncplane_putstr(n, file_ext(file));
			} else if (left_space >= 4) {
				// truncate prefix and extension, keep dot
				x += ncplane_putchar(n, *file_name(file));
				x += ncplane_putwc(n, cfg.truncatechar);
				x += ncplane_putnstr(n, left_space - 3, file_ext(file));
				x += ncplane_putwc(n, cfg.truncatechar);
			} else if (left_space > 1) {
				x += ncplane_putnstr(n, left_space - 1, file_name(file));
				x += ncplane_putwc(n, cfg.truncatechar);
			} else {
				x += ncplane_putchar(n, *file_name(file));
			}
		} else {
			const int left_space = ncol - 3 - rightmargin;
			const int pre_len = strlen(file_name(file));
			if (left_space >= pre_len) {
				x += ncplane_putstr(n, file_name(file));
			} else if (left_space > 1) {
				x += ncplane_putnstr(n, left_space - 1, file_name(file));
				x += ncplane_putwc(n, cfg.truncatechar);
			} else {
				x += ncplane_putchar(n, *file_name(file));
			}
		}
	}

	for (; x < ncol - rightmargin - 2; x++)
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
		ncplane_putstr_yx(n, 0, 2, dir->error == -1 ? "malloc" : strerror(dir->error));
	} else if (dir->loading) {
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

		if (dir->length <= nrow)
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
				/* ncplane_on_styles(w, WA_DIM); */
				break;
			case 3:
				ncplane_on_styles(w, NCSTYLE_ITALIC);
				break;
			case 4:
				ncplane_on_styles(w, NCSTYLE_UNDERLINE);
				break;
			case 5:
				/* ncplane_on_styles(w, NCSTYLE_BLINK); */
				break;
			case 6: /* nothing */
				break;
			case 7:
				/* not supported, needs workaround */
				/* ncplane_on_styles(w, WA_REVERSE); */
				break;
			case 8:
				/* ncplane_on_styles(w, NCSTYLE_INVIS); */
				break;
			case 9: /* strikethrough */
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
