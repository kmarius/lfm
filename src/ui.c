#define _GNU_SOURCE
#ifndef NCURSES_WIDECHAR
#define NCURSES_WIDECHAR 1
#endif
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "app.h"
#include "async.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "nav.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#ifdef DEBUG
#define VERSION_FMT "v0.epsilon-debug"
#else
#define VERSION_FMT "v0.epsilon"
#endif

/* #define TRACE 1 */

#define COLORS_SEL 1000
#define COLORS_DEL 1001
#define COLORS_CPY 1002

#define COLORS_SEARCH 1003
#define ATTRS_SEARCH WA_REVERSE

void history_load(ui_t *ui);

#define WPREVIEW(ui) ui->wdirs[ui->ndirs - 1]

static void draw_info(ui_t *ui);
static void draw_cmdline(ui_t *ui);
static char *readable_fs(double size, char *buf);
static void menu_resize(ui_t *ui);
static char *wansi_consoom(WINDOW *w, char *s);
static void wdraw_file_preview(WINDOW *w, preview_t *pv);
static void wansi_addstr(WINDOW *w, char *s);

/* init/resize {{{ */
static void ncsetup()
{
	/* TODO: probably not all of this is needed on resize, colors in
	 * particular (on 2021-07-18) */
	initscr();
	keypad(stdscr, TRUE); /* Enable keyboard mapping */
	nonl();		      /* Tell curses not to do NL->CR/NL on output */
	cbreak(); /* Take input chars one at a time, no wait for \n */
	noecho(); /* Don't echo input to screen */
	raw();	  /* Allow ncurses to capture ctrl-c / ctrl-z */
	timeout(0);
	set_escdelay(0);
	curs_set(0);
	/* Make sure non-blocking mode is set on the keyboard */
	kbblocking(true);
	if (has_colors()) {
		use_default_colors();
		start_color();
		log_debug("%d color pairs, %d colors", COLOR_PAIRS, COLORS);

		/* color -1 means default background */
		init_pair(COLOR_BLACK, COLOR_BLACK, -1);
		init_pair(COLOR_GREEN, COLOR_GREEN, -1);
		init_pair(COLOR_RED, COLOR_RED, -1);
		init_pair(COLOR_CYAN, COLOR_CYAN, -1);
		init_pair(COLOR_WHITE, COLOR_WHITE, -1);
		init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
		init_pair(COLOR_BLUE, COLOR_BLUE, -1);
		init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);

		init_extended_pair(COLORS_SEL, COLOR_BLACK, 13);
		init_extended_pair(COLORS_DEL, COLOR_BLACK, COLOR_RED);
		init_extended_pair(COLORS_CPY, COLOR_BLACK, COLOR_YELLOW);
		init_extended_pair(COLORS_SEARCH, COLOR_YELLOW, -1);

		for (int i = 0; i < 256; i++) {
			init_extended_pair(i, i, -1);
		}
	}

	refresh();
}

void kbblocking(bool blocking)
{
	int val = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (val != -1) {
		if (blocking) {
			fcntl(STDIN_FILENO, F_SETFL, val & ~O_NONBLOCK);
		} else {
			fcntl(STDIN_FILENO, F_SETFL, val | O_NONBLOCK);
		}
	}
}

void ui_init(ui_t *ui)
{
#ifdef TRACE
	log_trace("ui_init");
#endif

	ncsetup();
	getmaxyx(stdscr, ui->nrow, ui->ncol);

	ui->wdirs = NULL;
	ui->dirs = NULL; /* set later in ui_draw */
	ui->ndirs = 0;
	ui->preview_dir = NULL;
	ui_recol(ui);

	ui->cmd_prefix[0] = 0;
	ui->cmd_acc_left[0] = 0;
	ui->cmd_acc_right[0] = 0;

	ui->infoline = newwin(1, ui->ncol, 0, 0);
	ui->cmdline = newwin(1, ui->ncol, ui->nrow - 1, 0);
	ui->menu = newwin(ui->nrow - 2, ui->ncol, 1, 0);

	ui->file_preview = NULL;
	ui->previews.size = 0;

	ui->menubuf = NULL;

	ui->history = NULL;
	ui->history_ptr = NULL;
	history_load(ui);

	ui->highlight = NULL;
	ui->search_forward = true;

	ui->load_mode = MODE_COPY;
	ui->load_sz = 0;

	ui->selection_sz = 0;

	draw_info(ui);
	draw_cmdline(ui);
}

/* maybe just expose resize to do this */
void ui_recol(ui_t *ui)
{
	/* log_trace("ui_recol"); */
	int i;

	cvector_fclear(ui->wdirs, delwin);

	ui->ndirs = cvector_size(cfg.ratios);

	int sum = 0;
	for (i = 0; i < ui->ndirs; i++) {
		sum += cfg.ratios[i];
	}

	int xpos = 0;
	for (i = 0; i < ui->ndirs - 1; i++) {
		int width = (ui->ncol - ui->ndirs + 1) * cfg.ratios[i] / sum;
		cvector_push_back(ui->wdirs, newwin(ui->nrow - 2, width, 1, xpos));
		xpos += width + 1;
	}
	cvector_push_back(ui->wdirs, newwin(ui->nrow - 2, ui->ncol - xpos - 1, 1, xpos));
}

void ui_resize(ui_t *ui)
{
	clear();
	endwin();
	ncsetup();

	getmaxyx(stdscr, ui->nrow, ui->ncol);

	wresize(ui->infoline, 1, ui->ncol);

	wresize(ui->cmdline, 1, ui->ncol);
	mvwin(ui->cmdline, ui->nrow - 1, 0);

	menu_resize(ui);

	ui_recol(ui);
}

/* }}} */

/* preview {{{ */

preview_t *ui_load_preview(ui_t *ui, file_t *file)
{
	/* log_trace("ui_load_preview %s", file->path); */

	int x, y;
	preview_t *pv = previewheap_take(&ui->previews, file->path);

	WINDOW *w = WPREVIEW(ui);
	getmaxyx(w, y, x);

	if (pv) {
		/* TODO: vv (on 2021-08-10) */
		/* might be checking too often here? or is it capped by inotify
		 * timeout? */
		if (!preview_check(pv)) {
			async_load_preview(pv->path, file, x, y);
		}
	} else {
		pv = new_loading_preview(file->path, file, x, y);
		async_load_preview(file->path, file, x, y);
	}
	return pv;
}

void ui_draw_preview(ui_t *ui)
{
	/* log_trace("ui_draw_preview"); */
	int x, y;
	getmaxyx(WPREVIEW(ui), y, x);
	dir_t *dir;
	file_t *file;

	WINDOW *w = WPREVIEW(ui);
	werase(w);

	dir = ui->dirs[0];
	if (dir && dir->ind < dir->len) {
		file = dir->files[dir->ind];
		if (ui->file_preview) {
			if (streq(ui->file_preview->path, file->path)) {
				if (!preview_check(ui->file_preview)) {
					async_load_preview(file->path, file, x, y);
				}
			} else {
				previewheap_insert(&ui->previews, ui->file_preview);
				ui->file_preview = ui_load_preview(ui, file);
			}
		} else {
			ui->file_preview = ui_load_preview(ui, file);
		}
		wdraw_file_preview(w, ui->file_preview);
	}
	wrefresh(w);
}

/* }}} */

/* wdraw_dir {{{ */

void search_nohighlight(ui_t *ui)
{
	free(ui->highlight);
	ui->highlight = NULL;
	ui->search_forward = true;
}

void search_highlight(ui_t *ui, const char *search, bool forward)
{
	ui->search_forward = forward;
	ui->highlight = strdup(search);
}

static void wdraw_file_line(WINDOW *w, file_t *file, bool iscurrent, char **sel,
		char **load, enum movemode_e mode,
		const char *highlight)
{
	int ncol, y0, x, _;
	char size[16];
	wchar_t buf[128];
	getmaxyx(w, _, ncol);
	getyx(w, y0, _);
	(void)_;

	const bool isexec = file_isexec(file);
	const bool isdir = file_isdir(file);
	const bool isselected = cvector_contains(file->path, sel);
	const bool iscpy =
		mode == MODE_COPY && cvector_contains(file->path, load);
	const bool isdel =
		mode == MODE_MOVE && cvector_contains(file->path, load);
	const bool islink = file->link_target != NULL;

	if (isdir) {
		snprintf(size, sizeof(size), "%d", file->filecount);
	} else {
		readable_fs(file->stat.st_size, size);
	}

	int rightmargin =
		strlen(size) + 2; /* one space before and one after size */
	if (islink) {
		rightmargin += 3; /* " ->" */
	}
	if (rightmargin > ncol * 2 / 3) {
		rightmargin = 0;
	}

	wattr_set(w, WA_NORMAL, 0, NULL);

	if (isselected) {
		wcolor_set(w, COLORS_SEL, NULL);
	} else if (isdel) {
		wcolor_set(w, COLORS_DEL, NULL);
	} else if (iscpy) {
		wcolor_set(w, COLORS_CPY, NULL);
	}
	waddch(w, ' ');
	wcolor_set(w, 0, NULL);
	if (isdir) {
		wattr_on(w, WA_BOLD, NULL);
		wcolor_set(w, COLOR_BLUE, NULL);
	} else if (isexec) {
		wcolor_set(w, COLOR_GREEN, NULL);
	}
	if (iscurrent) {
		wattr_on(w, A_REVERSE, NULL);
	}

	// this would be easier if we knew how long the printed filename is
	waddch(w, ' ');
	char *hlsubstr;
	if (highlight && (hlsubstr = strcasestr(file->name, highlight))) {
		const int l = hlsubstr - file->name;
		attr_t old_attrs;
		short old_colors;
		wattr_get(w, &old_attrs, &old_colors, NULL);
		waddnstr(w, file->name, l);
		wattr_set(w, ATTRS_SEARCH, COLORS_SEARCH, NULL);
		waddnstr(w, highlight, ncol - 3);
		wattr_set(w, old_attrs, old_colors, NULL);
		waddnstr(w, file->name + l + strlen(highlight), ncol - 3);
		getyx(w, _, x);
	} else {
		const char *p = file->name;
		x = mbsrtowcs(buf, &p, ncol-3, NULL);
		waddnwstr(w, buf, ncol - 3);
	}
	for (int l = x; l < ncol - 3; l++) {
		waddch(w, ' ');
	}
	if (x + rightmargin + 2> ncol) {
		const wchar_t s[2] = {cfg.truncatechar, L'\0'};
		mvwaddwstr(w, y0, ncol-rightmargin - 1, s);
	} else {
		wmove(w, y0, ncol - rightmargin);
	}
	if (rightmargin > 0) {
		if (islink) {
			waddstr(w, " ->");
		}
		waddch(w, ' ');
		waddstr(w, size);
		waddch(w, ' ');
	}
	wattr_set(w, WA_NORMAL, 0, NULL);
}

static void wdraw_dir(WINDOW *w, dir_t *dir, char **sel, char **load,
		enum movemode_e mode, const char *highlight)
{
	/* log_debug("wdraw_dir"); */

	int nrow, ncol, i, offset;
	(void)ncol;

	werase(w);
	getmaxyx(w, nrow, ncol);

	if (dir) {
		wattr_set(w, WA_NORMAL, 0, NULL);
		if (dir->error) {
			wattr_on(w, A_REVERSE, NULL);
			mvwaddstr(w, 0, 2, "error");
			/* TODO: distinguish errors (on 2021-07-30) */
		} else if (dir->loading) {
			wattr_on(w, A_REVERSE, NULL);
			mvwaddstr(w, 0, 2, "loading");
		} else if (dir->len == 0) {
			wattr_on(w, A_REVERSE, NULL);
			mvwaddstr(w, 0, 2, "empty");
		} else {
			dir->pos = min(min(dir->pos, nrow - 1), dir->ind);

			offset = max(dir->ind - dir->pos, 0);

			if (dir->len <= nrow) {
				offset = 0;
			}

			for (i = 0; i + offset < dir->len && i < nrow; i++) {
				wmove(w, i, 0);
				wdraw_file_line(w, dir->files[i + offset],
						i == dir->pos, sel, load, mode,
						highlight);
			}
		}
		wattr_set(w, WA_NORMAL, 0, NULL);
	}
	wrefresh(w);
}

static void wansi_matchattr(WINDOW *w, int a)
{
	/* log_debug("match_attr %d", a); */
	int fg = -1;
	int bg = -1;
	if (a >= 0 && a <= 9) {
		switch (a) {
			case 0:
				wattr_set(w, WA_NORMAL, 0, NULL);
				break;
			case 1:
				wattr_on(w, WA_BOLD, NULL);
				break;
			case 2:
				wattr_on(w, WA_DIM, NULL);
				break;
			case 3:
				wattr_on(w, WA_ITALIC, NULL);
				break;
			case 4:
				wattr_on(w, WA_UNDERLINE, NULL);
				break;
			case 5:
				wattr_on(w, WA_BLINK, NULL);
				break;
			case 6: /* nothing */
				break;
			case 7:
				wattr_on(w, WA_REVERSE, NULL);
				break;
			case 8:
				wattr_on(w, WA_INVIS, NULL);
				break;
			case 9: /* strikethrough */
				break;
			default:
				break;
		}
	} else if (a >= 30 && a <= 37) {
		fg = a - 30;
		wcolor_set(w, fg, NULL);
	} else if (a >= 40 && a <= 47) {
		bg = a - 40;
		wcolor_set(w, bg, NULL);
	}
	(void)fg;
	(void)bg;
}

/*
 * Consooms ansi color escape sequences and sets ATTRS
 * should be called with a pointer at \033
 */
static char *wansi_consoom(WINDOW *w, char *s)
{
	/* log_debug("consooming"); */
	char c;
	int acc = 0;
	int nnums = 0;
	int nums[6];
	s++;
	if (!(*s == '[')) {
		log_debug("there should be a [ here");
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
			case 0:
				log_debug("escape ended prematurely");
				fin = true;
				break;
			default:
				if (!(c >= '0' && c <= '9')) {
					log_debug("not a number? %c", c);
				}
				acc = 10 * acc + (c - '0');
		}
		if (nnums > 5) {
			log_debug("malformed ansi: too many numbers");
			/* TODO: seek to 'm' (on 2021-07-29) */
			return NULL;
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
		if (nums[0] == 38 && nums[1] == 5) {
			wcolor_set(w, nums[2], NULL);
		} else if (nums[0] == 48 && nums[1] == 5) {
			log_debug(
					"trying to set background color per ansi code");
		}
	} else if (nnums == 4) {
		wansi_matchattr(w, nums[0]);
		wcolor_set(w, nums[3], NULL);
	} else if (nnums == 5) {
		log_debug("using unsupported ansi code with 5 numbers");
		/* log_debug("%d %d %d %d %d", nums[0], nums[1], nums[2],
		 * nums[3], nums[4]);
		 */
	} else if (nnums == 6) {
		log_debug("using unsupported ansi code with 6 numbers");
		/* log_debug("%d %d %d %d %d %d", nums[0], nums[1], nums[2],
		 * nums[3], nums[4], nums[5]); */
	}

	return s;
}

static void wdraw_file_preview(WINDOW *w, preview_t *pv)
{
#ifdef TRACE
	log_trace("wdraw_preview");
#endif
	int i, ncol, nrow;
	char **it;

	getmaxyx(w, nrow, ncol);
	wattr_set(w, WA_NORMAL, 0, NULL);
	i = 0;
	for (it = cvector_begin(pv->lines); it != cvector_end(pv->lines);
			it++) {
		/* TODO: find out how to not wrap / print a fixed amout of
		 * visible chars (on 2021-07-27) */
		/* TODO: possibly find out how to convert a string to cchars (on
		 * 2021-07-27)
		 */
		/* mvwaddstr(w, i, 0, *it); */

		wmove(w, i, 0);

		wansi_addstr(w, *it);

		if (++i >= nrow) {
			break;
		}
	}
	if (i < nrow) {
		/* draw over wrapped line */
		wmove(w, i, 0);
		wprintw(w, "%*s", ncol, "");
	}
}

bool ui_insert_pv(ui_t *ui, preview_t *pv)
{
#ifdef TRACE
	log_debug("insert_pv %s %p", pv->path, pv);
#endif

	bool ret = false;

	const file_t *file = dir_current_file(ui->dirs[0]);
	if (file && (file == pv->fptr || streq(pv->path, file->path))) {
		free_preview(ui->file_preview);
		ui->file_preview = pv;
		ret = true;
	} else {
		preview_t **pvptr = previewheap_find(&ui->previews, pv->path);
		if (pvptr) {
			/* mtime is seconds right? >= is probably better */
			if (pv->mtime >= (*pvptr)->mtime) {
				/* update */
				free_preview(*pvptr);
				*pvptr = pv;
				previewheap_updatep(&ui->previews, pvptr, time(NULL));
			} else {
				/* discard */
				free_preview(pv);
			}
		} else {
			previewheap_insert(&ui->previews, pv);
		}
	}
	return ret;
}

/* }}} */

/* menu {{{ */

static void wansi_addstr(WINDOW *w, char *s)
{
	while (*s != 0) {
		if (*s == '\033') {
			s = wansi_consoom(w, s);
		} else {
			waddch(w, *(s++));
		}
	}
}

static void draw_menu(ui_t *ui)
{
	int y, x;
	int i;

	if (!ui->menubuf) {
		return;
	}

	getyx(ui->cmdline, y, x);
	for (i = 0; i < ui->menubuflen; i++) {
		/* wmove(ui->menu, i, 0); */
		/* wansi_addstr(ui->menu, ui->menubuf[i]); */
		mvwaddstr(ui->menu, i, 0, ui->menubuf[i]);

		wclrtoeol(ui->menu);
	}
	wrefresh(ui->menu);

	/* restore cursor */
	wmove(ui->cmdline, y, x);
	wrefresh(ui->cmdline);
}

static void menu_resize(ui_t *ui)
{
	const int h = min(ui->menubuflen, ui->nrow - 2);
	wresize(ui->menu, h, ui->ncol);
	mvwin(ui->menu, ui->nrow - 1 - h, 0);
}

static void clear_menu(ui_t *ui)
{
	/* clear window because some cells are not covered by the nav */
	int ncol, nrow, i;
	(void)ncol;
	if (!ui->menubuf) {
		return;
	}
	getmaxyx(ui->menu, nrow, ncol);
	for (i = 0; i < nrow; i++) {
		wmove(ui->menu, i, 0);
		wclrtoeol(ui->menu);
	}
	wrefresh(ui->menu);
}

void ui_showmenu(ui_t *ui, char **vec, int len)
{
	if (ui->menubuf) {
		clear_menu(ui);
		cvector_ffree(ui->menubuf, free);
		ui->menubuf = NULL;
		ui->menubuflen = 0;
	}
	if (len > 0) {
		ui->menubuf = vec;
		ui->menubuflen = len;
		menu_resize(ui);
		draw_menu(ui);
	}
}

/* }}} */

/* cmd line {{{ */

/* TODO: these should probably deal with wchars (on 2021-07-24) */
void ui_cmd_prefix_set(ui_t *ui, const char *prefix)
{
	log_debug("cmd_prefix_set %s", prefix);
	if (!prefix) {
		return;
	}
	curs_set(1);
	strncpy(ui->cmd_prefix, prefix, sizeof(ui->cmd_prefix) - 1);
	draw_cmdline(ui);
}

void ui_cmd_insert(ui_t *ui, char key)
{
	/* log_debug("cmd_insert %c", key); */
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	const int l = strlen(ui->cmd_acc_left);
	if (l >= ACC_SIZE - 1) {
		return;
	}
	ui->cmd_acc_left[l + 1] = 0;
	ui->cmd_acc_left[l] = key;
	draw_cmdline(ui);
}

void ui_cmd_delete(ui_t *ui)
{
	log_debug("cmd_delete");
	int l;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if ((l = strlen(ui->cmd_acc_left)) > 0) {
		ui->cmd_acc_left[l - 1] = 0;
		log_debug("cmd_delete: %s", ui->cmd_acc_left);
	}
	draw_cmdline(ui);
}

/* pass a ct argument to move over words? */
void ui_cmd_left(ui_t *ui)
{
	log_trace("cmd_left");
	int l, j;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if ((l = strlen(ui->cmd_acc_left)) > 0) {
		j = min(strlen(ui->cmd_acc_right), ACC_SIZE - 2);
		ui->cmd_acc_right[j + 1] = 0;
		for (; j > 0; j--) {
			ui->cmd_acc_right[j] = ui->cmd_acc_right[j - 1];
		}
		ui->cmd_acc_right[0] = ui->cmd_acc_left[l - 1];
		ui->cmd_acc_left[l - 1] = 0;
	}
	draw_cmdline(ui);
}

void ui_cmd_right(ui_t *ui)
{
	log_trace("cmd_right");
	int l, j;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if ((j = strlen(ui->cmd_acc_right)) > 0) {
		if ((l = strlen(ui->cmd_acc_left)) < ACC_SIZE - 2) {
			ui->cmd_acc_left[l] = ui->cmd_acc_right[0];
			ui->cmd_acc_left[l + 1] = 0;
			for (l = 0; l < j; l++) {
				ui->cmd_acc_right[l] = ui->cmd_acc_right[l + 1];
			}
		}
	}
	draw_cmdline(ui);
}

void ui_cmd_home(ui_t *ui)
{
	log_trace("cmd_home");
	int l, j;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if ((l = strlen(ui->cmd_acc_left)) > 0) {
		j = min(strlen(ui->cmd_acc_right) - 1 + l, ACC_SIZE - 1 - l);
		ui->cmd_acc_right[j + 1] = 0;
		for (; j >= l; j--) {
			ui->cmd_acc_right[j] = ui->cmd_acc_right[j - l];
		}
		strncpy(ui->cmd_acc_right, ui->cmd_acc_left, l);
		ui->cmd_acc_left[0] = 0;
	}
	draw_cmdline(ui);
}

void ui_cmd_end(ui_t *ui)
{
	log_trace("cmd_end");
	int l, j;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if (ui->cmd_acc_right[0] != 0) {
		j = strlen(ui->cmd_acc_left);
		l = ACC_SIZE - 1 - strlen(ui->cmd_acc_left);
		strncpy(ui->cmd_acc_left + j, ui->cmd_acc_right, l);
		ui->cmd_acc_left[l + j] = 0;
		ui->cmd_acc_right[0] = 0;
	}
	draw_cmdline(ui);
}

void ui_cmd_clear(ui_t *ui)
{
#ifdef TRACE
	log_debug("cmd_clear");
#endif
	ui->cmd_prefix[0] = 0;
	ui->cmd_acc_left[0] = 0;
	ui->cmd_acc_right[0] = 0;
	ui->history_ptr = NULL;
	curs_set(0);
	draw_cmdline(ui);
	ui_showmenu(ui, NULL, 0);
}

void ui_cmdline_set(ui_t *ui, const char *line)
{
	/* log_trace("ui_cmdline_set %s", line); */
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	strncpy(ui->cmd_acc_left, line, sizeof(ui->cmd_acc_left)-1);
	ui->cmd_acc_right[0] = 0;
	draw_cmdline(ui);
}

const char *ui_cmdline_get(const ui_t *ui)
{
	static char buf[2 * ACC_SIZE] = {0};
	if (ui->cmd_prefix[0] == 0) {
		return "";
	} else {
		snprintf(buf, sizeof(buf), "%s%s", ui->cmd_acc_left,
				ui->cmd_acc_right);
	}
	return buf;
}

static int filetypeletter(int mode)
{
	char c;

	if (S_ISREG(mode))
		c = '-';
	else if (S_ISDIR(mode))
		c = 'd';
	else if (S_ISBLK(mode))
		c = 'b';
	else if (S_ISCHR(mode))
		c = 'c';
#ifdef S_ISFIFO
	else if (S_ISFIFO(mode))
		c = 'p';
#endif /* S_ISFIFO */
#ifdef S_ISLNK
	else if (S_ISLNK(mode))
		c = 'l';
#endif /* S_ISLNK */
#ifdef S_ISSOCK
	else if (S_ISSOCK(mode))
		c = 's';
#endif /* S_ISSOCK */
#ifdef S_ISDOOR
	/* Solaris 2.6, etc. */
	else if (S_ISDOOR(mode))
		c = 'D';
#endif /* S_ISDOOR */
	else {
		/* Unknown type -- possibly a regular file? */
		c = '?';
	}
	return c;
}

static char *perms(int mode)
{
	static const char *rwx[] = {"---", "--x", "-w-", "-wx",
		"r--", "r-x", "rw-", "rwx"};
	static char bits[11];

	bits[0] = filetypeletter(mode);
	strcpy(&bits[1], rwx[(mode >> 6) & 7]);
	strcpy(&bits[4], rwx[(mode >> 3) & 7]);
	strcpy(&bits[7], rwx[(mode & 7)]);
	if (mode & S_ISUID)
		bits[3] = (mode & S_IXUSR) ? 's' : 'S';
	if (mode & S_ISGID)
		bits[6] = (mode & S_IXGRP) ? 's' : 'l';
	if (mode & S_ISVTX)
		bits[9] = (mode & S_IXOTH) ? 't' : 'T';
	bits[10] = '\0';
	return bits;
}

static char *owner(int uid)
{
	static char owner[32];
	struct passwd *pwd;
	if ((pwd = getpwuid(uid))) {
		strncpy(owner, pwd->pw_name, sizeof(owner)-1);
		owner[31] = 0;
	} else {
		owner[0] = 0;
	}

	return owner;
}

static char *group(int gid)
{
	static char group[32];
	struct group *grp;
	if ((grp = getgrgid(gid))) {
		strncpy(group, grp->gr_name, sizeof(group)-1);
		group[31] = 0;
	} else {
		group[0] = 0;
	}
	return group;
}

static char *readable_fs(double size, char *buf)
{
	int i = 0;
	const char *units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
	while (size > 1024) {
		size /= 1024;
		i++;
	}
	sprintf(buf, "%.*f%s", i > 0 ? 1 : 0, size, units[i]);
	return buf;
}

static char *print_time(time_t time, char *buffer, int bufsz)
{
	strftime(buffer, bufsz, "%Y-%m-%d %H:%M:%S", localtime(&time));
	return buffer;
}

static int int_sz(int n)
{
	int i = 1;
	while (n >= 10) {
		i++;
		n /= 10;
	}
	return i;
}

void draw_cmdline(ui_t *ui)
{
#ifdef TRACE
	log_trace("draw_cmdline");
#endif
	static char buf[512] = {0};
	static char nums[16];
	werase(ui->cmdline);
	if (ui->cmd_prefix[0] == 0) {
		dir_t *dir = ui->dirs ? ui->dirs[0] : NULL;
		if (dir && dir->ind < dir->len) {
			/* TODO: for empty directories, show the stat of the
			 * directory instead (on 2021-07-18) */
			file_t *cur = dir->files[dir->ind];
			static char size[32];
			static char mtime[32];
			const int lhs_sz = snprintf(
					buf, sizeof(buf), "%s %2.ld %s %s %4s %s%s%s",
					perms(cur->stat.st_mode), cur->stat.st_nlink,
					owner(cur->stat.st_uid), group(cur->stat.st_gid),
					readable_fs(cur->stat.st_size, size),
					print_time(cur->stat.st_mtime, mtime, sizeof(mtime)),
					cur->link_target ? " -> " : "",
					cur->link_target ? cur->link_target : "");
			int rhs_sz = snprintf(nums, sizeof(nums), " %d/%d", dir->ind + 1, dir->len);
			mvwaddstr(ui->cmdline, 0, 0, buf);
			mvwaddstr(ui->cmdline, 0, ui->ncol - rhs_sz, nums);
			/* TODO: add selection info in addition to cut/copy (on 2021-08-04) */
			if (ui->load_sz > 0) {
				if (ui->load_mode == MODE_COPY) {
					wcolor_set(ui->cmdline, COLORS_CPY, NULL);
				} else {
					wcolor_set(ui->cmdline, COLORS_DEL, NULL);
				}
				rhs_sz += int_sz(ui->load_sz) + 3;
				wmove(ui->cmdline, 0, ui->ncol - rhs_sz + 1);
				wprintw(ui->cmdline, " %d ", ui->load_sz);
				wattr_set(ui->cmdline, WA_NORMAL, 0, NULL);
				waddch(ui->cmdline, ' ');
			}
			if (ui->selection_sz > 0) {
				wcolor_set(ui->cmdline, COLORS_SEL, NULL);
				rhs_sz += int_sz(ui->selection_sz) + 3;
				wmove(ui->cmdline, 0, ui->ncol - rhs_sz + 1);
				wprintw(ui->cmdline, " %d ", ui->selection_sz);
				wattr_set(ui->cmdline, WA_NORMAL, 0, NULL);
				waddch(ui->cmdline, ' ');
			}
			if (lhs_sz + rhs_sz > ui->ncol) {
				mvwaddch(ui->cmdline, 0, ui->ncol - rhs_sz - 1, cfg.truncatechar);
				waddch(ui->cmdline, ' ');
			}
		}
	} else {
		const int cursor_pos =
			strlen(ui->cmd_prefix) + strlen(ui->cmd_acc_left);
		mvwaddstr(ui->cmdline, 0, 0, ui->cmd_prefix);
		waddstr(ui->cmdline, ui->cmd_acc_left);
		waddstr(ui->cmdline, ui->cmd_acc_right);
		wmove(ui->cmdline, 0, cursor_pos);
	}
	wrefresh(ui->cmdline);
}
/* }}} */

/* info line {{{ */

static void draw_info(ui_t *ui)
{
	// arbitrary
	static char user[32] = {0};
	static char host[HOST_NAME_MAX + 1] = {0};
	static char *home;
	static int home_len;
	int ncol, nrow;
	getmaxyx(ui->infoline, nrow, ncol);
	(void) nrow;

	werase(ui->infoline);
	if (user[0] == 0) {
		getlogin_r(user, 32);
		gethostname(host, HOST_NAME_MAX + 1);
		home = getenv("HOME");
		home_len = strlen(home);
	}

	wattr_on(ui->infoline, WA_BOLD, NULL);

	wcolor_set(ui->infoline, COLOR_GREEN, NULL);
	mvwaddstr(ui->infoline, 0, 0, user);
	waddch(ui->infoline, '@');
	waddstr(ui->infoline, host);
	wcolor_set(ui->infoline, 0, NULL);

	waddch(ui->infoline, ':');

	const dir_t *dir = ui->dirs ? ui->dirs[0] : NULL;
	if (dir) {
		wcolor_set(ui->infoline, COLOR_BLUE, NULL);
		if (home && hasprefix(dir->path, home)) {
			waddch(ui->infoline, '~');
			waddstr(ui->infoline, dir->path + home_len);
		} else {
			waddstr(ui->infoline, dir->path);
		}
	}
	wattr_set(ui->infoline, WA_NORMAL, 0, NULL);
	mvwaddstr(ui->infoline, 0, ncol-strlen(VERSION_FMT),  VERSION_FMT);
	wrefresh(ui->infoline);
}

/* }}} */

/* history {{{ */

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */

void history_write(ui_t *ui)
{
	log_trace("history_write");

	char *dir, *buf = strdup(cfg.historypath);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	FILE *fp = fopen(cfg.historypath, "w");
	if (!fp) {
		ui_error(ui, "history: %s", strerror(errno));
		return;
	}

	char **it;
	for (it = cvector_begin(ui->history); it != cvector_end(ui->history);
			++it) {
		fputs(*it, fp);
		fputc('\n', fp);
	}
	fclose(fp);
}

/* does *not* free the vector */
void history_clear(ui_t *ui)
{
	cvector_fclear(ui->history, free);
}

void history_load(ui_t *ui)
{
	char *line = NULL;
	ssize_t read;
	size_t n;
	FILE *fp;

	if (! (fp = fopen(cfg.historypath, "r"))) {
		error("history: %s", strerror(errno));
		return;
	}

	while ((read = getline(&line, &n, fp)) != -1) {
		line[strlen(line) - 1] = 0; /* remove \n */
		cvector_push_back(ui->history, line);
		line = NULL;
	}

	fclose(fp);
}

void ui_history_append(ui_t *ui, const char *line)
{
	char **end = cvector_end(ui->history);
	if (end && streq(*(end - 1), line)) {
		/* skip consecutive dupes */
		return;
	}
	cvector_push_back(ui->history, strdup(line));
}

/* TODO: only show history items with matching prefixes (on 2021-07-24) */
const char *ui_history_prev(ui_t *ui)
{
	if (!ui->history_ptr) {
		ui->history_ptr = cvector_end(ui->history);
	}
	if (!ui->history_ptr) {
		return NULL;
	}
	if (ui->history_ptr > cvector_begin(ui->history)) {
		--ui->history_ptr;
	}
	return *ui->history_ptr;
}

const char *ui_history_next(ui_t *ui)
{
	if (!ui->history_ptr) {
		return NULL;
	}
	if (ui->history_ptr < cvector_end(ui->history)) {
		++ui->history_ptr;
	}
	if (ui->history_ptr == cvector_end(ui->history)) {
		return "";
	}
	return *ui->history_ptr;
}
/* }}} */

/* main drawing/echo/err {{{ */
void ui_draw(ui_t *ui, nav_t *nav)
{
	ui_draw_dirs(ui, nav);
	draw_menu(ui);
	draw_cmdline(ui);
}

/* to not overwrite errors */
void ui_draw_dirs(ui_t *ui, nav_t *nav)
{
#ifdef TRACE
	log_trace("ui_draw");
#endif

	int i;

	ui->dirs = nav->dirs;
	ui->preview_dir = nav->preview;

	nav->height = ui->nrow - 2;
	ui->selection_sz = nav->selection_len;
	ui->load_sz = nav->load_len;
	ui->load_mode = nav->mode;
	draw_info(ui);

	const int l = nav->ndirs;
	for (i = 0; i < l; i++) {
		wdraw_dir(ui->wdirs[l-i-1], ui->dirs[i], nav->selection, nav->load,
				nav->mode, i == 0 ? ui->highlight : NULL);
	}

	if (cfg.preview) {
		dir_t *pdir = ui->preview_dir;
		if (pdir) {
			wdraw_dir(WPREVIEW(ui), ui->preview_dir, nav->selection,
					nav->load, nav->mode, NULL);
		} else {
			/* TODO: reload preview after resize (on 2021-07-27) */
			ui_draw_preview(ui);
		}
	}
}

void ui_clear(ui_t *ui, nav_t *nav)
{
	clear();
	refresh();
	ui_draw(ui, nav);
}

void ui_echom(ui_t *ui, const char *format, ...)
{
	char msg[1024];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	werase(ui->cmdline);
	mvwaddstr(ui->cmdline, 0, 0, msg);
	touchwin(ui->cmdline);
	wrefresh(ui->cmdline);
}

void ui_error(ui_t *ui, const char *format, ...)
{
	char msg[1024];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	log_error(msg);

	werase(ui->cmdline);
	wcolor_set(ui->cmdline, COLOR_RED, NULL);
	mvwaddstr(ui->cmdline, 0, 0, msg);
	wattr_set(ui->cmdline, WA_NORMAL, 0, NULL);
	wrefresh(ui->cmdline);
}

/* }}} */

void ui_destroy(ui_t *ui)
{
	history_write(ui);
	history_clear(ui);
	cvector_free(ui->history);
	cvector_ffree(ui->menubuf, free);
	int i;
	for (i = 0; i < ui->previews.size; i++) {
		free_preview(ui->previews.previews[i]);
	}
	cvector_ffree(ui->wdirs, delwin);
	free(ui->highlight);
	delwin(ui->menu);
	delwin(ui->cmdline);
	delwin(ui->infoline);
	delwin(stdscr);
	endwin();
}
