#define _GNU_SOURCE
#include <notcurses/notcurses.h>
/* #include <ncurses.h> */
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

#define TRACE 1

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_PINK 5
#define COLOR_TEAL 6

#define COLOR_SEL COLOR_PINK
#define COLOR_DEL COLOR_RED
#define COLOR_CPY COLOR_YELLOW

#define COLOR_SEARCH COLOR_YELLOW
#define STYLE_SEARCH NCSTYLE_BOLD

#define WPREVIEW(ui) ui->wdirs[ui->ndirs - 1]

static void history_load(ui_t *ui);
static void draw_info(ui_t *ui);
static void draw_cmdline(ui_t *ui);
static char *readable_fs(double size, char *buf);
static void menu_resize(ui_t *ui);
static char *wansi_consoom(struct ncplane *w, char *s);
static void wdraw_file_preview(struct ncplane *n, preview_t *pv);
static void wansi_addstr(struct ncplane *w, char *s);
static int rsize(struct ncplane *n);

static struct notcurses *nc;
static struct ncplane *ncstd;

/* init/resize {{{ */
static void ncsetup()
{
	struct notcurses_options opts = {
		.flags = NCOPTION_NO_WINCH_SIGHANDLER,
	};
	nc = notcurses_core_init(&opts, NULL);
	if (!nc) {
		exit(EXIT_FAILURE);
	}
	ncstd = notcurses_stdplane(nc);
}

void ui_kbblocking(bool blocking)
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

void ui_init(ui_t *ui, nav_t *nav)
{
#ifdef TRACE
	log_trace("ui_init");
#endif
	ui->nav = nav;

	ncsetup();
	ui->input_ready_fd = notcurses_inputready_fd(nc);
	ui->nc = nc;
	ncplane_dim_yx(ncstd, &ui->nrow, &ui->ncol);
	/* log_debug("%d, %d", ui->nrow, ui->ncol); */

	ui->wdirs = NULL;
	ui->dirs = NULL; /* set later in ui_draw */
	ui->ndirs = 0;
	ui->preview_dir = NULL;

	ui->cmd_prefix[0] = 0;
	ui->cmd_acc_left[0] = 0;
	ui->cmd_acc_right[0] = 0;

	struct ncplane_options opts = {
		.y = 0,
		.x = 0,
		.rows = 1,
		.cols = ui->ncol,
		.userptr = ui,
	};

	opts.resizecb = rsize;
	ui->infoline = ncplane_create(ncstd, &opts);
	opts.resizecb = NULL;

	opts.y = ui->nrow-1;
	ui->cmdline = ncplane_create(ncstd, &opts);

	ui->menubuf = NULL;
	ui->menu = NULL;

	ui_recol(ui);

	opts.rows = opts.cols = 1;
	ui->menu = ncplane_create(ncstd, &opts);
	ncplane_move_bottom(ui->menu);

	ui->file_preview = NULL;
	ui->previews.size = 0;

	ui->history = NULL;
	ui->history_ptr = NULL;
	history_load(ui);

	ui->highlight = NULL;
	ui->search_forward = true;

	ui->load_mode = MODE_COPY;
	ui->load_sz = 0;

	ui->selection_sz = 0;
}

void ui_recol(ui_t *ui)
{
	/* log_trace("ui_recol"); */
	int i;

	cvector_fclear(ui->wdirs, ncplane_destroy);

	ui->ndirs = cvector_size(cfg.ratios);

	int sum = 0;
	for (i = 0; i < ui->ndirs; i++) {
		sum += cfg.ratios[i];
	}

	struct ncplane_options opts = {
		.y = 1,
		.rows = ui->nrow - 2,
	};

	int xpos = 0;
	for (i = 0; i < ui->ndirs - 1; i++) {
		opts.cols = (ui->ncol - ui->ndirs + 1) * cfg.ratios[i] / sum;
		opts.x = xpos;
		cvector_push_back(ui->wdirs, ncplane_create(ncstd, &opts));
		xpos += opts.cols + 1;
	}
	opts.x = xpos;
	opts.cols = ui->ncol - xpos - 1;
	cvector_push_back(ui->wdirs, ncplane_create(ncstd, &opts));
}

void ui_resize(ui_t *ui)
{
	notcurses_stddim_yx(nc, &ui->nrow, &ui->ncol);
	log_debug("ui_resize %d %d", ui->nrow, ui->ncol);
	ncplane_resize(ui->infoline, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
	ncplane_resize(ui->cmdline, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
	ncplane_move_yx(ui->cmdline, ui->nrow - 1, 0);
	menu_resize(ui);
	ui_recol(ui);
	ui_clear(ui);
}

static int rsize(struct ncplane *n)
{
	ui_resize(ncplane_userptr(n));
	return 0;
}

/* }}} */

/* preview {{{ */

preview_t *ui_load_preview(ui_t *ui, file_t *file)
{
	/* log_trace("ui_load_preview %s", file->path); */

	int x, y;
	preview_t *pv = previewheap_take(&ui->previews, file->path);

	struct ncplane *w = WPREVIEW(ui);
	ncplane_dim_yx(w, &y, &x);

	if (pv) {
		/* TODO: vv (on 2021-08-10) */
		/* might be checking too often here? or is it capped by inotify
		 * timeout? */
		if (!preview_check(pv)) {
			async_preview_load(pv->path, file, x, y);
		}
	} else {
		pv = preview_new_loading(file->path, file, x, y);
		async_preview_load(file->path, file, x, y);
	}
	return pv;
}

void ui_draw_preview(ui_t *ui)
{
	/* log_trace("ui_draw_preview"); */
	int x, y;
	ncplane_dim_yx(WPREVIEW(ui), &y, &x);
	dir_t *dir;
	file_t *file;

	struct ncplane *w = WPREVIEW(ui);
	ncplane_erase(w);

	dir = ui->dirs[0];
	if (dir && dir->ind < dir->len) {
		file = dir->files[dir->ind];
		if (ui->file_preview) {
			if (streq(ui->file_preview->path, file->path)) {
				if (!preview_check(ui->file_preview)) {
					async_preview_load(file->path, file, x, y);
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
	notcurses_render(nc);
}

/* }}} */

/* wdraw_dir {{{ */

void ui_search_nohighlight(ui_t *ui)
{
	free(ui->highlight);
	ui->highlight = NULL;
	ui->search_forward = true;
}

void ui_search_highlight(ui_t *ui, const char *search, bool forward)
{
	ui->search_forward = forward;
	ui->highlight = strdup(search);
}

static void wdraw_file_line(struct ncplane *n, file_t *file, bool iscurrent, char **sel,
		char **load, enum movemode_e mode,
		const char *highlight)
{
	int ncol, y0, x, _;
	char size[16];
	wchar_t buf[128];
	ncplane_dim_yx(n, &_, &ncol);
	ncplane_cursor_yx(n, &y0, &_);
	(void)_;

	const bool isexec = file_isexec(file);
	const bool isdir = file_isdir(file);
	const bool isselected = cvector_contains(file->path, sel);
	const bool iscpy =
		mode == MODE_COPY && cvector_contains(file->path, load);
	const bool isdel =
		mode == MODE_MOVE && cvector_contains(file->path, load);
	const bool islink = file->link_target != NULL;

	/* log_debug("%s %u %u", file->name, ncplane_fg_rgb(n), ncplane_bg_rgb(n)); */

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

	ncplane_set_bg_default(n);

	if (isselected) {
		ncplane_set_bg_palindex(n, COLOR_SEL);
	} else if (isdel) {
		ncplane_set_bg_palindex(n, COLOR_DEL);
	} else if (iscpy) {
		ncplane_set_bg_palindex(n, COLOR_CPY);
	}
	ncplane_putchar(n, ' ');
	ncplane_set_bg_default(n);
	if (isdir) {
		ncplane_set_fg_palindex(n, COLOR_BLUE);
		ncplane_set_styles(n, NCSTYLE_BOLD);
	} else if (isexec) {
		ncplane_set_fg_palindex(n, COLOR_GREEN);

	} else if (hascasesuffix(".mp4", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".mkv", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".m4v", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".webm", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".avi", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".flv", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".wmv", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);
	} else if (hascasesuffix(".avi", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_PINK);

	} else if (hascasesuffix(".tar", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".zst", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".xz", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".gz", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".zip", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".rar", file->name)) {
		ncplane_set_fg_palindex(n, 9);
	} else if (hascasesuffix(".7z", file->name)) {
		ncplane_set_fg_palindex(n, 9);

	} else if (hascasesuffix(".mp3", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".m4a", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".ogg", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".flag", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".mka", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);

	} else if (hascasesuffix(".jpg", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".jpeg", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".png", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".bmp", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".webp", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);
	} else if (hascasesuffix(".gif", file->name)) {
		ncplane_set_fg_palindex(n, COLOR_YELLOW);

	} else {
		ncplane_set_fg_default(n);
	}

	if (iscurrent) {
		ncplane_set_bg_palindex(n, 237);
	}

	// this would be easier if we knew how long the printed filename is
	char *hlsubstr;
	ncplane_putchar(n, ' ');
	if (highlight && (hlsubstr = strcasestr(file->name, highlight))) {
		const int l = hlsubstr - file->name;
		const uint64_t ch = ncplane_channels(n);
		ncplane_putnstr(n, l, file->name);
		ncplane_set_bg_palindex(n, COLOR_YELLOW);
		ncplane_set_fg_palindex(n, COLOR_BLACK);
		ncplane_putnstr(n, ncol-3, highlight);
		ncplane_set_channels(n, ch);
		ncplane_putnstr(n, ncol-3, file->name + l + strlen(highlight));
		ncplane_cursor_yx(n, NULL, &x);
	} else {
		const char *p = file->name;
		x = mbsrtowcs(buf, &p, ncol-3, NULL);
		ncplane_putnstr(n, ncol-3, file->name);
	}
	for (int l = x; l < ncol - 3; l++) {
		ncplane_putchar(n, ' ');
	}
	if (x + rightmargin + 2> ncol) {
		ncplane_putwc_yx(n, y0, ncol-rightmargin - 1, cfg.truncatechar);
	} else {
		ncplane_cursor_move_yx(n, y0, ncol - rightmargin);
	}
	if (rightmargin > 0) {
		if (islink) {
			ncplane_putstr(n, " ->");
		}
		ncplane_putchar(n, ' ');
		ncplane_putstr(n, size);
		ncplane_putchar(n, ' ');
	}
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);
	ncplane_set_styles(n, NCSTYLE_NONE);
}

static void wdraw_dir(struct ncplane *n, dir_t *dir, char **sel, char **load,
		enum movemode_e mode, const char *highlight)
{
	int nrow, i, offset;

	ncplane_erase(n);
	ncplane_dim_yx(n, &nrow, NULL);
	ncplane_cursor_move_yx(n, 0, 0);

	if (dir) {
#ifdef TRACE
		log_debug("wdraw_dir %s", dir->name);
#endif
		if (dir->error) {
		/* EACCES Permission denied. */
		/* EBADF  fd is not a valid file descriptor opened for reading.
		*/
		/* EMFILE The per-process limit on the number of open file */
		/*        descriptors has been reached. */
		/* ENFILE The system-wide limit on the total number of open
		 * files */
		/*        has been reached. */
		/* ENOENT Directory does not exist, or name is an empty string.
		*/
		/* ENOMEM Insufficient memory to complete the operation. */
		/* ENOTDIR */
			ncplane_putstr_yx(n, 0, 2, dir->error == -1 ? "malloc" : strerror(dir->error));
		} else if (dir->loading) {
			ncplane_putstr_yx(n, 0, 2, "loading");
		} else if (dir->len == 0) {
			ncplane_putstr_yx(n, 0, 2, "empty");
		} else {
			dir->pos = min(min(dir->pos, nrow - 1), dir->ind);

			offset = max(dir->ind - dir->pos, 0);

			if (dir->len <= nrow) {
				offset = 0;
			}

			for (i = 0; i + offset < dir->len && i < nrow; i++) {
				ncplane_cursor_move_yx(n, i, 0);
				wdraw_file_line(n, dir->files[i + offset],
						i == dir->pos, sel, load, mode,
						highlight);
			}
		}
	}
}

static void wansi_matchattr(struct ncplane *w, int a)
{
	/* log_debug("match_attr %d", a); */
	int fg = -1;
	int bg = -1;
	if (a >= 0 && a <= 9) {
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
				ncplane_on_styles(w, NCSTYLE_BLINK);
				break;
			case 6: /* nothing */
				break;
			case 7:
				/* not supported, needs workaround */
				/* ncplane_on_styles(w, WA_REVERSE); */
				break;
			case 8:
				ncplane_on_styles(w, NCSTYLE_INVIS);
				break;
			case 9: /* strikethrough */
				break;
			default:
				break;
		}
	} else if (a >= 30 && a <= 37) {
		fg = a - 30;
		ncplane_set_fg_palindex(w, fg);
	} else if (a >= 40 && a <= 47) {
		bg = a - 40;
		ncplane_set_bg_palindex(w, bg);
	}
	(void)fg;
	(void)bg;
}

/*
 * Consooms ansi color escape sequences and sets ATTRS
 * should be called with a pointer at \033
 */
static char *wansi_consoom(struct ncplane *w, char *s)
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
			ncplane_set_fg_palindex(w, nums[2]);
		} else if (nums[0] == 48 && nums[1] == 5) {
			log_debug(
					"trying to set background color per ansi code");
		}
	} else if (nnums == 4) {
		wansi_matchattr(w, nums[0]);
		ncplane_set_fg_palindex(w, nums[3]);
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

static void wdraw_file_preview(struct ncplane *w, preview_t *pv)
{
#ifdef TRACE
	log_trace("wdraw_preview");
#endif
	int i, ncol, nrow;

	ncplane_dim_yx(w, &nrow, &ncol);
	ncplane_set_styles(w, NCSTYLE_NONE);
	ncplane_set_fg_default(w);
	ncplane_set_bg_default(w);

	const int l = cvector_size(pv->lines);
	for (i = 0; i < l && i < nrow; i++) {
		/* TODO: find out how to not wrap / print a fixed amout of
		 * visible chars (on 2021-07-27) */
		/* TODO: possibly find out how to convert a string to cchars (on
		 * 2021-07-27)
		 */

		ncplane_cursor_move_yx(w, i, 0);
		wansi_addstr(w, pv->lines[i]);
		/* ncplane_putnstr_yx(w, i, 0, ncol, pv->lines[i]); */
	}
}

bool ui_insert_preview(ui_t *ui, preview_t *pv)
{
	log_debug("ui_insert_preview %s", pv->path);

	bool ret = false;

	const file_t *file = dir_current_file(ui->dirs[0]);
	if (file && (file == pv->fptr || streq(pv->path, file->path))) {
		preview_free(ui->file_preview);
		ui->file_preview = pv;
		ret = true;
	} else {
		preview_t **pvptr = previewheap_find(&ui->previews, pv->path);
		if (pvptr) {
			/* mtime is seconds right? >= is probably better */
			if (pv->mtime >= (*pvptr)->mtime) {
				/* update */
				pv->access = (*pvptr)->access;
				preview_free(*pvptr);
				*pvptr = pv;
			} else {
				/* discard */
				preview_free(pv);
			}
		} else {
			previewheap_insert(&ui->previews, pv);
		}
	}
	return ret;
}

/* }}} */

/* menu {{{ */

static void wansi_addstr(struct ncplane *w, char *s)
{
	while (*s) {
		if (*s == '\033') {
			s = wansi_consoom(w, s);
		} else {
			if (ncplane_putchar(w, *(s++)) == -1) {
				// EOL
				return;
			}
		}
	}
}

static void draw_menu(ui_t *ui)
{
	int i;
	struct ncplane *n = ui->menu;

	if (!ui->menubuf) {
		return;
	}

	ncplane_erase(n);

	/* otherwise this doesn't draw over the directories */
	ncplane_set_base(ui->menu, 0, 0, ' ');

	for (i = 0; i < ui->menubuflen; i++) {
		ncplane_cursor_move_yx(n, i, 0);
		const char *s = ui->menubuf[i];
		int xpos = 0;

		while (*s) {
			while (*s && *s != '\t') {
				ncplane_putchar(n, *(s++));
				xpos++;
			}
			if (*s == '\t') {
				ncplane_putchar(n, ' ');
				s++;
				xpos++;
				for (const int l = ((xpos/8)+1)*8; xpos < l; xpos++) {
					ncplane_putchar(n, ' ');
				}
			}
		}
	}

	notcurses_render(nc);
}

static void menu_resize(ui_t *ui)
{
	const int h = max(1, min(ui->menubuflen, ui->nrow - 2));
	ncplane_resize(ui->menu, 0, 0, 0, 0, 0, 0, h, ui->ncol);
	ncplane_move_yx(ui->menu, ui->nrow - 1 - h, 0);
}

static void menu_clear(ui_t *ui)
{
	if (!ui->menubuf) {
		return;
	}
	ncplane_erase(ui->menu);
	ncplane_move_bottom(ui->menu);
	notcurses_render(nc);
}

void ui_showmenu(ui_t *ui, char **vec, int len)
{
	if (ui->menubuf) {
		menu_clear(ui);
		cvector_ffree(ui->menubuf, free);
		ui->menubuf = NULL;
		ui->menubuflen = 0;
	}
	if (len > 0) {
		ui->menubuf = vec;
		ui->menubuflen = len;
		menu_resize(ui);
		ncplane_move_top(ui->menu);
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
	/* curs_set(1); */
	notcurses_cursor_enable(nc, 0, 0);
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
	notcurses_cursor_disable(nc);
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
	ncplane_erase(ui->cmdline);
	ncplane_set_bg_default(ui->cmdline);
	ncplane_set_fg_default(ui->cmdline);

	if (ui->cmd_prefix[0] == 0) {
		const dir_t *dir = ui->dirs[0];
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
			ncplane_putstr_yx(ui->cmdline, 0, 0, buf);
			ncplane_putstr_yx(ui->cmdline, 0, ui->ncol - rhs_sz, nums);
			ncplane_set_fg_palindex(ui->cmdline, 0);
			if (ui->load_sz > 0) {
				if (ui->load_mode == MODE_COPY) {
					ncplane_set_bg_palindex(ui->cmdline, COLOR_CPY);
				} else {
					ncplane_set_bg_palindex(ui->cmdline, COLOR_DEL);
				}
				rhs_sz += int_sz(ui->load_sz) + 3;
				ncplane_printf_yx(ui->cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->load_sz);
				ncplane_set_bg_default(ui->cmdline);
				ncplane_putchar(ui->cmdline, ' ');
			}
			if (ui->selection_sz > 0) {
				ncplane_set_bg_palindex(ui->cmdline, COLOR_SEL);
				rhs_sz += int_sz(ui->selection_sz) + 3;
				ncplane_printf_yx(ui->cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->selection_sz);
				ncplane_set_bg_default(ui->cmdline);
				ncplane_putchar(ui->cmdline, ' ');
			}
			if (lhs_sz + rhs_sz > ui->ncol) {
				ncplane_putwc_yx(ui->cmdline, 0, ui->ncol - rhs_sz - 1, cfg.truncatechar);
				ncplane_putchar(ui->cmdline, ' ');
			}
		}
	} else {
		const int cursor_pos = strlen(ui->cmd_prefix) + strlen(ui->cmd_acc_left);
		ncplane_putstr_yx(ui->cmdline, 0, 0, ui->cmd_prefix);
		ncplane_putstr(ui->cmdline, ui->cmd_acc_left);
		ncplane_putstr(ui->cmdline, ui->cmd_acc_right);
		notcurses_cursor_enable(nc, ui->nrow - 1, cursor_pos);
	}
	notcurses_render(nc);
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

	ncplane_erase(ui->infoline);

	if (user[0] == 0) {
		getlogin_r(user, sizeof(user));
		gethostname(host, sizeof(host));
		home = getenv("HOME");
		home_len = strlen(home);
	}

	ncplane_set_fg_default(ui->infoline);
	ncplane_set_styles(ui->infoline, NCSTYLE_NONE);
	ncplane_putstr_yx(ui->infoline, 0, ui->ncol-strlen(VERSION_FMT),  VERSION_FMT);

	ncplane_set_styles(ui->infoline, NCSTYLE_BOLD);
	ncplane_set_fg_palindex(ui->infoline, COLOR_GREEN);
	ncplane_putstr_yx(ui->infoline, 0, 0, user);
	ncplane_putchar(ui->infoline, '@');
	ncplane_putstr(ui->infoline, host);
	ncplane_set_fg_default(ui->infoline);

	ncplane_set_styles(ui->infoline, NCSTYLE_NONE);
	ncplane_putchar(ui->infoline, ':');
	ncplane_set_styles(ui->infoline, NCSTYLE_BOLD);

	const dir_t *dir = ui->dirs[0];
	if (dir) {
		// shortening should work fine with ascii only names
		const file_t *file = dir_current_file(dir);
		const char *end = dir->path + strlen(dir->path);
		int remaining;
		ncplane_cursor_yx(ui->infoline, NULL, &remaining);
		remaining = ui->ncol - remaining;
		if (file) {
			remaining -= strlen(file->name);
		}
		ncplane_set_fg_palindex(ui->infoline, COLOR_BLUE);
		const char *c = dir->path;
		if (home && hasprefix(dir->path, home)) {
			ncplane_putchar(ui->infoline, '~');
			remaining--;
			c += home_len;
		}
		while (*c && end - c > remaining) {
			ncplane_putchar(ui->infoline, '/');
			ncplane_putchar(ui->infoline, *(++c));
			remaining -= 2;
			while (*(++c) && (*c != '/'))
				;
		}
		ncplane_putstr(ui->infoline, c);
		if (!dir_isroot(dir)) {
			ncplane_putchar(ui->infoline, '/');
		}
		if (file) {
			ncplane_set_fg_default(ui->infoline);
			ncplane_putstr(ui->infoline, file->name);
		}
	}
	notcurses_render(nc);
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
		app_error("history: %s", strerror(errno));
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
	log_trace("ui_draw_dirs");
#endif

	ui->dirs = nav->dirs;
	ui->preview_dir = nav->preview;

	nav->height = ui->nrow - 2;
	ui->selection_sz = nav->selection_len;
	ui->load_sz = nav->load_len;
	ui->load_mode = nav->mode;
	draw_info(ui);

	const int l = nav->ndirs;
	int i;
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
	notcurses_render(nc);
}

void ui_clear(ui_t *ui)
{
	log_debug("ui_clear");

	/* infoline and dirs have to be cleared *and* rendered, otherwise they will
	 * bleed into the first row */
	ncplane_erase(ncstd);
	ncplane_erase(ui->infoline);
	for (int i = 0; i < ui->ndirs; i++) {
		ncplane_erase(ui->wdirs[i]);
	}
	ncplane_erase(ui->cmdline);

	notcurses_render(nc);

	notcurses_refresh(nc, NULL, NULL);

	ui_draw(ui, ui->nav);
}

void ui_echom(ui_t *ui, const char *format, ...)
{
	char msg[128];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	ncplane_erase(ui->cmdline);
	ncplane_set_fg_palindex(ui->cmdline, 15);
	ncplane_putstr_yx(ui->cmdline, 0, 0, msg);
	ncplane_set_fg_default(ui->cmdline);
	notcurses_render(nc);
}

void ui_error(ui_t *ui, const char *format, ...)
{
	char msg[128];
	va_list args;
	va_start(args, format);
	vsprintf(msg, format, args);
	va_end(args);

	log_error(msg);

	ncplane_erase(ui->cmdline);
	ncplane_set_fg_palindex(ui->cmdline, COLOR_RED);
	ncplane_putstr_yx(ui->cmdline, 0, 0, msg);
	ncplane_set_fg_default(ui->cmdline);
	notcurses_render(nc);
}

/* }}} */

void ui_destroy(ui_t *ui)
{
	int i;
	history_write(ui);
	history_clear(ui);
	cvector_free(ui->history);
	cvector_ffree(ui->menubuf, free);
	for (i = 0; i < ui->previews.size; i++) {
		preview_free(ui->previews.previews[i]);
	}
	free(ui->highlight);
	notcurses_stop(nc);
}
