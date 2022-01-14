#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <ncurses.h>
#include <notcurses/notcurses.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "app.h"
#include "async.h"
#include "cache.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "fm.h"
#include "history.h"
#include "log.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#define PROFILE_ENABLE 0

#if PROFILE_ENABLE
#define PROFILE_BEGIN(__t0) \
	const unsigned long (__t0) = current_micros();
#define PROFILE_END(__t0) \
	log_trace("%s finished after %.2fms", __FUNCTION__, (current_micros() - (__t0))/1000.0);
#else
#define PROFILE_BEGIN(__t0)
#define PROFILE_END(__t0)
#endif

static void draw_dirs(ui_t *ui);
static void plane_draw_dir(struct ncplane *n, Dir *dir, char **sel,
		char **load, enum movemode_e mode, const char *highlight);
static void draw_cmdline(ui_t *ui);
static void draw_preview(ui_t *ui);
static void plane_draw_file_preview(struct ncplane *n, preview_t *pv);
static void update_file_preview(ui_t *ui);
static void draw_menu(struct ncplane *n, cvector_vector_type(char *) menu);
static void draw_info(ui_t *ui);
static char *readable_fs(double size, char *buf);
static void menu_resize(ui_t *ui);
static char *ansi_consoom(struct ncplane *w, char *s);
static void ansi_addstr(struct ncplane *n, char *s);

static struct notcurses *nc = NULL;

/* init/resize {{{ */

static int resize_cb(struct ncplane *n)
{
	/* TODO: dir->pos needs to be changed for all directories (on 2021-10-30) */
	ui_t *ui = ncplane_userptr(n);
	notcurses_stddim_yx(nc, &ui->nrow, &ui->ncol);
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

void ui_notcurses_init(ui_t *ui)
{
	struct notcurses_options ncopts = {
		.flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR,
	};
	if ((nc = notcurses_core_init(&ncopts, NULL)) == NULL) {
		exit(EXIT_FAILURE);
	}
	struct ncplane *ncstd = notcurses_stdplane(nc);

	ncplane_dim_yx(ncstd, &ui->nrow, &ui->ncol);
	ui->nc = nc;
	ui->fm->height = ui->nrow - 2;

	struct ncplane_options opts = {
		.y = 0,
		.x = 0,
		.rows = 1,
		.cols = ui->ncol,
		.userptr = ui,
	};

	opts.resizecb = resize_cb;
	ui->planes.info = ncplane_create(ncstd, &opts);
	opts.resizecb = NULL;

	opts.y = ui->nrow-1;
	ui->planes.cmdline = ncplane_create(ncstd, &opts);

	ui_recol(ui);

	opts.rows = opts.cols = 1;
	ui->planes.menu = ncplane_create(ncstd, &opts);
	ncplane_move_bottom(ui->planes.menu);
}

void ui_suspend(ui_t *ui)
{
	notcurses_stop(nc);
	nc = NULL;
	ui->planes.dirs = NULL;
	ui->planes.cmdline = NULL;
	ui->planes.menu = NULL;
	ui->planes.info = NULL;
}

void ui_init(ui_t *ui, fm_t *fm)
{
	ui->fm = fm;

	cache_init(&ui->preview.cache, PREVIEW_CACHE_SIZE, (void(*)(void*)) preview_free);
	cmdline_init(&ui->cmdline);
	history_load(&ui->history, cfg.historypath);

	ui->planes.dirs = NULL;
	ui->planes.cmdline = NULL;
	ui->planes.menu = NULL;
	ui->planes.info = NULL;

	ui->ndirs = 0;

	ui->preview.file = NULL;

	ui->highlight = NULL;

	ui->menubuf = NULL;
	ui->message = false;

	ui_notcurses_init(ui);

	log_info("initialized ui");
}

void kbblocking(bool blocking)
{
	int val = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (val != -1) {
		fcntl(STDIN_FILENO, F_SETFL, blocking ? val & ~O_NONBLOCK : val | O_NONBLOCK);
	}
}

void ui_recol(ui_t *ui)
{
	int i;
	struct ncplane *ncstd = notcurses_stdplane(ui->nc);

	cvector_fclear(ui->planes.dirs, ncplane_destroy);

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
		cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
		xpos += opts.cols + 1;
	}
	opts.x = xpos;
	opts.cols = ui->ncol - xpos - 1;
	cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
	ui->planes.preview = ui->planes.dirs[ui->ndirs-1];
}

/* }}} */

/* main drawing/echo/err {{{ */

void ui_draw(ui_t *ui)
{
	PROFILE_BEGIN(t0);
	if (ui->redraw.fm) {
		draw_dirs(ui);
	}
	if (ui->redraw.fm | ui->redraw.menu) {
		draw_menu(ui->planes.menu, ui->menubuf);
	}
	if (ui->redraw.fm | ui->redraw.cmdline) {
		draw_cmdline(ui);
	}
	if (ui->redraw.fm | ui->redraw.info) {
		draw_info(ui);
	}
	if (ui->redraw.fm | ui->redraw.preview) {
		draw_preview(ui);
	}
	if (ui->redraw.fm | ui->redraw.cmdline | ui->redraw.info
			| ui->redraw.menu | ui->redraw.preview) {
		notcurses_render(nc);
		PROFILE_END(t0);
	}
	ui->redraw.fm = 0;
	ui->redraw.info = 0;
	ui->redraw.cmdline = 0;
	ui->redraw.menu = 0;
	ui->redraw.preview = 0;
}

void ui_clear(ui_t *ui)
{
	/* infoline and dirs have to be cleared *and* rendered, otherwise they will
	 * bleed into the first row */
	ncplane_erase(notcurses_stdplane(ui->nc));
	ncplane_erase(ui->planes.info);
	for (int i = 0; i < ui->ndirs; i++) {
		ncplane_erase(ui->planes.dirs[i]);
	}
	ncplane_erase(ui->planes.cmdline);

	notcurses_render(nc);

	notcurses_refresh(nc, NULL, NULL);

	notcurses_cursor_enable(nc, 0, 0);
	notcurses_cursor_disable(nc);

	ui->redraw.fm = 1;
}

static void draw_dirs(ui_t *ui)
{
	PROFILE_BEGIN(t0);
	int i;
	const int l = ui->fm->dirs.len;
	for (i = 0; i < l; i++) {
		plane_draw_dir(ui->planes.dirs[l-i-1],
				ui->fm->dirs.visible[i],
				ui->fm->selection.files,
				ui->fm->load.files,
				ui->fm->load.mode,
				i == 0 ? ui->highlight : NULL);
	}
	PROFILE_END(t0);
}

static void draw_preview(ui_t *ui)
{
	PROFILE_BEGIN(t0);
	if (cfg.preview && ui->ndirs > 1) {
		if (ui->fm->dirs.preview != NULL) {
			plane_draw_dir(ui->planes.preview, ui->fm->dirs.preview, ui->fm->selection.files,
					ui->fm->load.files, ui->fm->load.mode, NULL);
		} else {
			update_file_preview(ui);
			plane_draw_file_preview(ui->planes.preview, ui->preview.file);
		}
	}
	PROFILE_END(t0);
}

void ui_echom(ui_t *ui, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	ui_vechom(ui, format, args);
	va_end(args);
}

void ui_error(ui_t *ui, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	ui_verror(ui, format, args);
	va_end(args);
}

void ui_verror(ui_t *ui, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	log_error(msg);

	cvector_push_back(ui->messages, msg);

	/* TODO: show messages after initialization (on 2021-10-30) */
	if (nc != NULL) {
		ncplane_erase(ui->planes.cmdline);
		ncplane_set_fg_palindex(ui->planes.cmdline, COLOR_RED);
		ncplane_putstr_yx(ui->planes.cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->planes.cmdline);
		notcurses_render(nc);
		ui->message = true;
	}
}

void ui_vechom(ui_t *ui, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	cvector_push_back(ui->messages, msg);

	if (nc != NULL) {
		ncplane_erase(ui->planes.cmdline);
		ncplane_set_fg_palindex(ui->planes.cmdline, 15);
		ncplane_putstr_yx(ui->planes.cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->planes.cmdline);
		notcurses_render(nc);
		ui->message = true;
	}
}

/* }}} */

/* cmd line {{{ */

void ui_cmd_prefix_set(ui_t *ui, const char *prefix)
{
	if (prefix == NULL) {
		return;
	}
	ui->message = false;
	notcurses_cursor_enable(nc, 0, 0);
	cmdline_prefix_set(&ui->cmdline, prefix);
	ui->redraw.cmdline = 1;
}

void ui_cmd_clear(ui_t *ui)
{
	cmdline_clear(&ui->cmdline);
	history_reset(&ui->history);
	notcurses_cursor_disable(nc);
	ui_showmenu(ui, NULL);
	ui->redraw.cmdline = 1;
	ui->redraw.menu = 1;
}

static char filetypeletter(int mode)
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
	if ((pwd = getpwuid(uid)) != NULL) {
		strncpy(owner, pwd->pw_name, sizeof(owner)-1);
		owner[31] = 0;
	} else {
		owner[0] = 0;
	}

	return owner;
}

/* getgrgid() is somewhat slow, so we cache one call */
static char *group(int gid)
{
	static char group[32];
	static int cached_gid = INT_MAX;
	struct group *grp;

	if (gid == cached_gid) {
		return group;
	}

	if ((grp = getgrgid(gid)) != NULL) {
		strncpy(group, grp->gr_name, sizeof(group)-1);
		group[31] = 0;
		cached_gid = gid;
	} else {
		cached_gid = INT_MAX;
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
	snprintf(buf, sizeof(buf)-1, "%.*f%s", i > 0 ? 1 : 0, size, units[i]);
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
	PROFILE_BEGIN(t0);

	char nums[16];
	char size[32];
	char mtime[32];
	const Dir *dir;
	const File *file;

	if (ui->message) {
		return;
	}

	ncplane_erase(ui->planes.cmdline);
	/* sometimes the color is changed to grey */
	ncplane_set_bg_default(ui->planes.cmdline);
	ncplane_set_fg_default(ui->planes.cmdline);

	int rhs_sz = 0;
	int lhs_sz = 0;

	if (cmdline_prefix_get(&ui->cmdline) == NULL) {
		if ((dir = ui->fm->dirs.visible[0]) != NULL) {
			if ((file = dir_current_file(dir)) != NULL) {
				/* TODO: for empty directories, show the stat of the
				 * directory instead (on 2021-07-18) */
				lhs_sz = ncplane_printf_yx(ui->planes.cmdline, 0, 0,
						"%s %2.ld %s %s %4s %s%s%s",
						perms(file->lstat.st_mode), file->lstat.st_nlink,
						owner(file->lstat.st_uid), group(file->lstat.st_gid),
						readable_fs(file->stat.st_size, size),
						print_time(file->lstat.st_mtime, mtime, sizeof(mtime)),
						file->link_target ? " -> " : "",
						file->link_target ? file->link_target : "");
			}

			rhs_sz = snprintf(nums, sizeof(nums), " %d/%d", dir->length ? dir->ind + 1 : 0, dir->length);
			ncplane_putstr_yx(ui->planes.cmdline, 0, ui->ncol - rhs_sz, nums);

			if (dir->filter[0] != 0) {
				rhs_sz += strlen(dir->filter) + 3;
				ncplane_set_bg_palindex(ui->planes.cmdline, COLOR_GREEN);
				ncplane_set_fg_palindex(ui->planes.cmdline, COLOR_BLACK);
				ncplane_printf_yx(ui->planes.cmdline, 0, ui->ncol-rhs_sz+1, " %s ", dir->filter);
				ncplane_set_bg_default(ui->planes.cmdline);
				ncplane_set_fg_default(ui->planes.cmdline);
				ncplane_putchar(ui->planes.cmdline, ' ');
			}
			if (cvector_size(ui->fm->load.files) > 0) {
				if (ui->fm->load.mode == MODE_COPY) {
					ncplane_set_channels(ui->planes.cmdline, cfg.colors.copy);
				} else {
					ncplane_set_channels(ui->planes.cmdline, cfg.colors.delete);
				}
				rhs_sz += int_sz(cvector_size(ui->fm->load.files)) + 3;
				ncplane_printf_yx(ui->planes.cmdline, 0, ui->ncol-rhs_sz+1, " %lu ", cvector_size(ui->fm->load.files));
				ncplane_set_bg_default(ui->planes.cmdline);
				ncplane_putchar(ui->planes.cmdline, ' ');
			}
			if (ui->fm->selection.len > 0) {
				ncplane_set_channels(ui->planes.cmdline, cfg.colors.selection);
				rhs_sz += int_sz(ui->fm->selection.len) + 3;
				ncplane_printf_yx(ui->planes.cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->fm->selection.len);
				ncplane_set_bg_default(ui->planes.cmdline);
				ncplane_putchar(ui->planes.cmdline, ' ');
			}
			if (lhs_sz + rhs_sz > ui->ncol) {
				ncplane_putwc_yx(ui->planes.cmdline, 0, ui->ncol - rhs_sz - 1, cfg.truncatechar);
				ncplane_putchar(ui->planes.cmdline, ' ');
			}
		}
	} else {
		const int cursor_pos = cmdline_print(&ui->cmdline, ui->planes.cmdline);
		notcurses_cursor_enable(nc, ui->nrow - 1, cursor_pos);
	}
	PROFILE_END(t0);
}
/* }}} */

/* info line {{{ */

static void draw_info(ui_t *ui)
{
	PROFILE_BEGIN(t0);

	// arbitrary
	static char user[32] = {0};
	static char host[HOST_NAME_MAX + 1] = {0};
	static char *home;
	static int home_len;

	const Dir *dir;
	const File *file;

	ncplane_erase(ui->planes.info);

	if (user[0] == 0) {
		getlogin_r(user, sizeof(user));
		gethostname(host, sizeof(host));
		home = getenv("HOME");
		home_len = strlen(home);
	}

	ncplane_set_styles(ui->planes.info, NCSTYLE_BOLD);
	ncplane_set_fg_palindex(ui->planes.info, COLOR_GREEN);
	ncplane_putstr_yx(ui->planes.info, 0, 0, user);
	ncplane_putchar(ui->planes.info, '@');
	ncplane_putstr(ui->planes.info, host);
	ncplane_set_fg_default(ui->planes.info);

	ncplane_set_styles(ui->planes.info, NCSTYLE_NONE);
	ncplane_putchar(ui->planes.info, ':');
	ncplane_set_styles(ui->planes.info, NCSTYLE_BOLD);

	if ((dir = ui->fm->dirs.visible[0]) != NULL) {
		// shortening should work fine with ascii only names
		const char *end = dir->path + strlen(dir->path);
		int remaining;
		ncplane_cursor_yx(ui->planes.info, NULL, &remaining);
		remaining = ui->ncol - remaining;
		if ((file = dir_current_file(dir)) != NULL) {
			remaining -= strlen(file->name);
		}
		ncplane_set_fg_palindex(ui->planes.info, COLOR_BLUE);
		const char *c = dir->path;
		if (home != NULL && hasprefix(dir->path, home)) {
			ncplane_putchar(ui->planes.info, '~');
			remaining--;
			c += home_len;
		}
		while (*c != 0 && end - c > remaining) {
			ncplane_putchar(ui->planes.info, '/');
			ncplane_putchar(ui->planes.info, *(++c));
			remaining -= 2;
			while (*(++c) != 0 && (*c != '/'))
				;
		}
		ncplane_putstr(ui->planes.info, c);
		if (!dir_isroot(dir)) {
			ncplane_putchar(ui->planes.info, '/');
		}
		if (file != NULL) {
			ncplane_set_fg_default(ui->planes.info);
			ncplane_putstr(ui->planes.info, file->name);
		}
	}
	PROFILE_END(t0);
}

/* }}} */

/* menu {{{ */

static void ansi_addstr(struct ncplane *n, char *s)
{
	char *c;

	while (*s != 0) {
		if (*s == '\033') {
			s = ansi_consoom(n, s);
		} else {
			for (c = s; *s != 0 && *s != '\033'; s++);
			if (ncplane_putnstr(n, s-c, c) == -1) {
				// EOL
				return;
			}
		}
	}
}

static void draw_menu(struct ncplane *n, cvector_vector_type(char*) menubuf)
{
	size_t i;

	if (menubuf == NULL) {
		return;
	}

	PROFILE_BEGIN(t0);

	ncplane_erase(n);

	/* otherwise this doesn't draw over the directories */
	/* Still needed as of 2021-08-18 */
	ncplane_set_base(n, 0, 0, ' ');

	for (i = 0; i < cvector_size(menubuf); i++) {
		ncplane_cursor_move_yx(n, i, 0);
		const char *s = menubuf[i];
		int xpos = 0;

		while (*s != 0) {
			while (*s != 0 && *s != '\t') {
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
	PROFILE_END(t0);
}

static void menu_resize(ui_t *ui)
{
	/* TODO: find out why, after resizing, the menu is behind the dirs (on 2021-10-30) */
	const int h = max(1, min(cvector_size(ui->menubuf), ui->nrow - 2));
	ncplane_resize(ui->planes.menu, 0, 0, 0, 0, 0, 0, h, ui->ncol);
	ncplane_move_yx(ui->planes.menu, ui->nrow - 1 - h, 0);
}

static void menu_clear(ui_t *ui)
{
	if (ui->menubuf == NULL) {
		return;
	}
	ncplane_erase(ui->planes.menu);
	ncplane_move_bottom(ui->planes.menu);
}

void ui_showmenu(ui_t *ui, cvector_vector_type(char*) vec)
{
	if (ui->menubuf != NULL) {
		menu_clear(ui);
		cvector_ffree(ui->menubuf, free);
		ui->menubuf = NULL;
	}
	if (cvector_size(vec) > 0) {
		ui->menubuf = vec;
		menu_resize(ui);
		ncplane_move_top(ui->planes.menu);
	}
	ui->redraw.menu = 1;
}

/* }}} */

/* wdraw_dir {{{ */

static unsigned long ext_channel_find(const char *ext)
{
	size_t i;
	if (ext != NULL) {
		for (i = 0; i < cvector_size(cfg.colors.ext_channels); i++) {
			if (strcaseeq(ext, cfg.colors.ext_channels[i].ext)) {
				return cfg.colors.ext_channels[i].channel;
			}
		}
	}
	return 0;
}

static void print_file(struct ncplane *n, const File *file,
		bool iscurrent, char **sel, char **load, enum movemode_e mode,
		const char *highlight)
{
	int ncol, y0, x;
	char size[16];
	wchar_t buf[256];
	ncplane_dim_yx(n, NULL, &ncol);
	ncplane_cursor_yx(n, &y0, NULL);

	bool isdir = file_isdir(file);
	bool islink = file_islink(file);

	if (isdir) {
		if (file->filecount == -2) {
			snprintf(size, sizeof(size), "?");
		} else {
			snprintf(size, sizeof(size), "%d", file->filecount);
		}
	} else {
		readable_fs(file->stat.st_size, size);
	}

	int rightmargin = strlen(size) + 2;
	if (islink) {
		rightmargin += 3; /* " ->" */
	}
	if (rightmargin > ncol * 2 / 3) {
		rightmargin = 0;
	}

	ncplane_set_bg_default(n);

	if (cvector_contains(file->path, sel)) {
		ncplane_set_channels(n, cfg.colors.selection);
	} else if (mode == MODE_MOVE && cvector_contains(file->path, load)) {
		ncplane_set_channels(n, cfg.colors.delete);
	} else if (mode == MODE_COPY && cvector_contains(file->path, load)) {
		ncplane_set_channels(n, cfg.colors.copy);
	}

	// this is needed because when selecting with space the filename is printed
	// as black (bug in notcurses)
	// 2021-08-21
	ncplane_set_fg_default(n);

	ncplane_putchar(n, ' ');
	ncplane_set_fg_default(n);
	ncplane_set_bg_default(n);

	if (isdir) {
		ncplane_set_channels(n, cfg.colors.dir);
		ncplane_set_styles(n, NCSTYLE_BOLD);
	} else if (file->broken) {
		ncplane_set_channels(n, cfg.colors.broken);
	} else if (file_isexec(file)) {
		ncplane_set_channels(n, cfg.colors.exec);
	} else {
		unsigned long ch = ext_channel_find(file->ext);
		if (ch > 0) {
			ncplane_set_channels(n, ch);
		} else {
			ncplane_set_channels(n, cfg.colors.normal);
			/* ncplane_set_fg_default(n); */
		}
	}

	if (iscurrent) {
		ncplane_set_bchannel(n, cfg.colors.current);
	}

	char *hlsubstr;
	ncplane_putchar(n, ' ');
	if (highlight != NULL && (hlsubstr = strcasestr(file->name, highlight)) != NULL) {
		const int l = hlsubstr - file->name;
		const unsigned long ch = ncplane_channels(n);
		ncplane_putnstr(n, l, file->name);
		ncplane_set_channels(n, cfg.colors.search);
		ncplane_putnstr(n, strlen(highlight), file->name + l);
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

static void plane_draw_dir(struct ncplane *n, Dir *dir, char **sel, char **load,
		enum movemode_e mode, const char *highlight)
{
	int nrow, i, offset;

	ncplane_erase(n);
	ncplane_dim_yx(n, &nrow, NULL);
	ncplane_cursor_move_yx(n, 0, 0);

	if (dir != NULL) {
		if (dir->error) {
			ncplane_putstr_yx(n, 0, 2, dir->error == -1 ? "malloc" : strerror(dir->error));
		} else if (dir->loading) {
			ncplane_putstr_yx(n, 0, 2, "loading");
		} else if (dir->length == 0) {
			ncplane_putstr_yx(n, 0, 2, "empty");
		} else {
			dir->pos = min(min(dir->pos, nrow - 1), dir->ind);

			offset = max(dir->ind - dir->pos, 0);

			if (dir->length <= nrow) {
				offset = 0;
			}

			const int l = min(dir->length - offset, nrow);
			for (i = 0; i < l; i++) {
				ncplane_cursor_move_yx(n, i, 0);
				print_file(n, dir->files[i + offset],
						i == dir->pos, sel, load, mode, highlight);
			}
		}
	}
}
/* }}} */

/* preview {{{ */

static preview_t *load_preview(ui_t *ui, File *file)
{
	int ncol, nrow;
	preview_t *pv;

	ncplane_dim_yx(ui->planes.preview, &nrow, &ncol);

	if ((pv = cache_take(&ui->preview.cache, file->path)) != NULL) {
		/* TODO: vv (on 2021-08-10) */
		/* might be checking too often here? or is it capped by inotify
		 * timeout? */
		if (pv->nrow < ui->nrow - 2) {
			async_preview_load(pv->path, nrow);
			pv->loading = true;
		} else {
			async_preview_check(pv);
		}
	} else {
		pv = preview_new_loading(file->path, nrow);
		async_preview_load(file->path, nrow);
	}
	return pv;
}

static void update_file_preview(ui_t *ui)
{
	int ncol, nrow;
	ncplane_dim_yx(ui->planes.preview, &nrow, &ncol);
	Dir *dir;
	File *file;

	/* struct ncplane *w = wpreview(ui); */
	// ncplane_erase(w); /* TODO: why (on 2021-10-30) */

	if ((dir = ui->fm->dirs.visible[0]) != NULL && dir->ind < dir->length) {
		file = dir->files[dir->ind];
		if (ui->preview.file != NULL) {
			if (streq(ui->preview.file->path, file->path)) {
				if (!ui->preview.file->loading) {
					if (ui->preview.file->nrow < nrow) {
						async_preview_load(file->path, nrow);
						ui->preview.file->loading = true;
					} else {
						async_preview_check(ui->preview.file);
					}
				}
			} else {
				cache_insert(&ui->preview.cache, ui->preview.file, ui->preview.file->path);
				ui->preview.file = load_preview(ui, file);
				ui->redraw.preview = 1;
			}
		} else {
			ui->preview.file = load_preview(ui, file);
			ui->redraw.preview = 1;
		}
	} else {
		if (ui->preview.file != NULL) {
			cache_insert(&ui->preview.cache, ui->preview.file, ui->preview.file->path);
			ui->preview.file = NULL;
			ui->redraw.preview = 1;
		}
	}
}

static void wansi_matchattr(struct ncplane *w, int a)
{
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
	int acc = 0;
	int nnums = 0;
	int nums[6];
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
		if (nums[0] == 38 && nums[1] == 5) {
			ncplane_set_fg_palindex(w, nums[2]);
		} else if (nums[0] == 48 && nums[1] == 5) {
			log_error("trying to set background color per ansi code");
		}
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

static void plane_draw_file_preview(struct ncplane *n, preview_t *pv)
{
	int nrow;
	size_t i;

	ncplane_erase(n);

	if (pv != NULL) {
		ncplane_dim_yx(n, &nrow, NULL);
		ncplane_set_styles(n, NCSTYLE_NONE);
		ncplane_set_fg_default(n);
		ncplane_set_bg_default(n);

		for (i = 0; i < cvector_size(pv->lines) && i < (size_t)nrow; i++) {
			ncplane_cursor_move_yx(n, i, 0);
			ansi_addstr(n, pv->lines[i]);
		}
	}
}

bool ui_insert_preview(ui_t *ui, preview_t *pv)
{
	const File *file = fm_current_file(ui->fm);

	if (file != NULL && streq(pv->path, file->path)) {
		preview_free(ui->preview.file);
		ui->preview.file = pv;
		return true;
	} else {
		preview_t *oldpv = cache_take(&ui->preview.cache, pv->path);
		if (oldpv != NULL) {
			if (pv->mtime >= oldpv->mtime) {
				preview_free(oldpv);
				cache_insert(&ui->preview.cache, pv, pv->path);
			} else {
				/* discard */
				preview_free(pv);
			}
		} else {
			cache_insert(&ui->preview.cache, pv, pv->path);
		}
	}
	return false;
}

void ui_drop_cache(ui_t *ui)
{
	preview_free(ui->preview.file);
	ui->preview.file = NULL;
	cache_clear(&ui->preview.cache);
	update_file_preview(ui);
	ui->redraw.cmdline = 1;
	ui->redraw.preview = 1;
}

/* }}} */

void ui_deinit(ui_t *ui)
{
	history_write(&ui->history, cfg.historypath);
	history_deinit(&ui->history);
	cvector_ffree(ui->messages, free);
	cvector_ffree(ui->menubuf, free);
	cache_deinit(&ui->preview.cache);
	cmdline_deinit(&ui->cmdline);
	cvector_ffree(ui->planes.dirs, ncplane_destroy);
	ui_suspend(ui);
}
