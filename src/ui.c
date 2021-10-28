#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
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
#include "history.h"
#include "log.h"
#include "nav.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

static bool init = false;

inline static struct ncplane *wpreview(ui_t *ui)
{
	return ui->wdirs[ui->ndirs-1];
}

void request_draw_cmdline(ui_t *ui);
static void draw_info(ui_t *ui);
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
		.flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS,
	};
	if (!(nc = notcurses_core_init(&opts, NULL))) {
		exit(EXIT_FAILURE);
	}
	ncstd = notcurses_stdplane(nc);
}

void kbblocking(bool blocking)
{
	int val = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (val != -1) {
		fcntl(STDIN_FILENO, F_SETFL, blocking ? val & ~O_NONBLOCK : val | O_NONBLOCK);
	}
}

void ui_init(ui_t *ui, nav_t *nav)
{
	ui->nav = nav;

	cache_init(&ui->previewcache, PREVIEW_CACHE_SIZE, (void(*)(void*)) preview_free);

	ncsetup();
	ui->input_ready_fd = notcurses_inputready_fd(nc);
	ui->nc = nc;
	ncplane_dim_yx(ncstd, &ui->nrow, &ui->ncol);
	nav->height = ui->nrow - 2;

	ui->wdirs = NULL;
	ui->ndirs = 0;

	cmdline_init(&ui->cmdline);

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
	ui->plane_cmdline = ncplane_create(ncstd, &opts);

	ui->menubuf = NULL;
	ui->menu = NULL;

	ui_recol(ui);

	opts.rows = opts.cols = 1;
	ui->menu = ncplane_create(ncstd, &opts);
	ncplane_move_bottom(ui->menu);

	ui->file_preview = NULL;

	history_load(&ui->history, cfg.historypath);

	/* ui->messages = NULL; */

	ui->highlight = NULL;
	ui->highlight_active = false;
	ui->search_forward = true;

	init = true;
	log_info("initialized ui");
}

void ui_recol(ui_t *ui)
{
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
	ncplane_resize(ui->plane_cmdline, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
	ncplane_move_yx(ui->plane_cmdline, ui->nrow - 1, 0);
	/* if (ui->file_preview) { */
	/* 	preview_free(ui->file_preview); */
	/* 	ui->file_preview = NULL; */
	/* } */
	menu_resize(ui);
	ui_recol(ui);
	ui->nav->height = ui->nrow - 2;
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
	int ncol, nrow;
	preview_t *pv;

	struct ncplane *w = wpreview(ui);
	ncplane_dim_yx(w, &nrow, &ncol);

	if ((pv = cache_take(&ui->previewcache, file->path))) {
		/* TODO: vv (on 2021-08-10) */
		/* might be checking too often here? or is it capped by inotify
		 * timeout? */
		if (!preview_check(pv) || pv->nrow < ui->nrow - 2) {
			async_preview_load(pv->path, file, nrow, ncol);
		}
	} else {
		pv = preview_new_loading(file->path, file, nrow, ncol);
		async_preview_load(file->path, file, nrow, ncol);
	}
	return pv;
}

void ui_draw_preview(ui_t *ui)
{
	wdraw_file_preview(wpreview(ui), ui->file_preview);
}

static void update_preview(ui_t *ui)
{
	int ncol, nrow;
	ncplane_dim_yx(wpreview(ui), &nrow, &ncol);
	dir_t *dir;
	file_t *file;

	struct ncplane *w = wpreview(ui);
	ncplane_erase(w);

	if ((dir = ui->nav->dirs[0]) && dir->ind < dir->len) {
		file = dir->files[dir->ind];
		if (ui->file_preview) {
			if (streq(ui->file_preview->path, file->path)) {
				if ((!preview_check(ui->file_preview)
							|| ui->file_preview->nrow < nrow)
						&& !ui->file_preview->loading){
					// avoid loading more previews when drawing
					ui->file_preview->loading = true;
					async_preview_load(file->path, file, nrow, ncol);
				}
			} else {
				cache_insert(&ui->previewcache, ui->file_preview, ui->file_preview->path);
				ui->file_preview = ui_load_preview(ui, file);
			}
		} else {
			ui->file_preview = ui_load_preview(ui, file);
		}
	} else {
		if (ui->file_preview) {
			cache_insert(&ui->previewcache, ui->file_preview, ui->file_preview->path);
			ui->file_preview = NULL;
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
		ncplane_set_fg_palindex(w, a-30);
	} else if (a >= 40 && a <= 47) {
		ncplane_set_bg_palindex(w, a-40);
	}
}

/*
 * Consooms ansi color escape sequences and sets ATTRS
 * should be called with a pointer at \033
 */
static char *wansi_consoom(struct ncplane *w, char *s)
{
	char c;
	int acc = 0;
	int nnums = 0;
	int nums[6];
	s++; // first char guaranteed to be \033
	if (!(*s == '[')) {
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

static void wdraw_file_preview(struct ncplane *n, preview_t *pv)
{
	int i, nrow;

	ncplane_erase(n);

	if (pv) {
		ncplane_dim_yx(n, &nrow, NULL);
		ncplane_set_styles(n, NCSTYLE_NONE);
		ncplane_set_fg_default(n);
		ncplane_set_bg_default(n);

		const int l = cvector_size(pv->lines);
		for (i = 0; i < l && i < nrow; i++) {
			ncplane_cursor_move_yx(n, i, 0);
			wansi_addstr(n, pv->lines[i]);
		}
	}

	notcurses_render(nc);
}

bool ui_insert_preview(ui_t *ui, preview_t *pv)
{
	preview_t *oldpv;
	const file_t *file;

	if ((file = nav_current_file(ui->nav)) && (file == pv->fptr || streq(pv->path, file->path))) {
		preview_free(ui->file_preview);
		ui->file_preview = pv;
		return true;
	} else {
		if ((oldpv = cache_take(&ui->previewcache, pv->path))) {
			if (pv->mtime >= oldpv->mtime) {
				preview_free(oldpv);
				cache_insert(&ui->previewcache, pv, pv->path);
			} else {
				/* discard */
				preview_free(pv);
			}
		} else {
			cache_insert(&ui->previewcache, pv, pv->path);
		}
	}
	return false;
}

void ui_drop_cache(ui_t *ui)
{
	preview_free(ui->file_preview);
	ui->file_preview = NULL;
	cache_clear(&ui->previewcache);
	update_preview(ui);
	ui_request_draw(ui);
}

/* }}} */

/* search {{{ */
void ui_search_nohighlight(ui_t *ui)
{
	ui->highlight_active = false;
}

void ui_search_highlight(ui_t *ui, const char *search, bool forward)
{
	if (search) {
		ui->search_forward = forward;
		if (ui->highlight) {
			free(ui->highlight);
		}
		ui->highlight = strdup(search);
	}
	ui->highlight_active = true;
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

static void draw_menu(struct ncplane *n, cvector_vector_type(char*) menubuf)
{
	size_t i;

	if (!menubuf) {
		return;
	}

	ncplane_erase(n);

	/* otherwise this doesn't draw over the directories */
	/* Still needed as of 2021-08-18 */
	ncplane_set_base(n, 0, 0, ' ');

	for (i = 0; i < cvector_size(menubuf); i++) {
		ncplane_cursor_move_yx(n, i, 0);
		const char *s = menubuf[i];
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
	const int h = max(1, min(cvector_size(ui->menubuf), ui->nrow - 2));
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
	}
	if (len > 0) {
		ui->menubuf = vec;
		menu_resize(ui);
		ncplane_move_top(ui->menu);
		draw_menu(ui->menu, ui->menubuf);
	}
}

/* }}} */

/* cmd line {{{ */

void ui_cmd_prefix_set(ui_t *ui, const char *prefix)
{
	if (!prefix) {
		return;
	}
	notcurses_cursor_enable(nc, 0, 0);
	cmdline_prefix_set(&ui->cmdline, prefix);
	request_draw_cmdline(ui);
}

/* inserts only the first mbchar of the argument */
void ui_cmd_insert(ui_t *ui, const char *key)
{
	if (cmdline_insert(&ui->cmdline, key)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_delete(ui_t *ui)
{
	if (cmdline_delete(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_delete_right(ui_t *ui)
{
	if (cmdline_delete_right(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

/* pass a ct argument to move over words? */
void ui_cmd_left(ui_t *ui)
{
	if (cmdline_left(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_right(ui_t *ui)
{
	if (cmdline_right(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_home(ui_t *ui)
{
	if (cmdline_home(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_end(ui_t *ui)
{
	if (cmdline_end(&ui->cmdline)) {
		request_draw_cmdline(ui);
	}
}

void ui_cmd_clear(ui_t *ui)
{
	cmdline_clear(&ui->cmdline);
	history_reset(&ui->history);
	notcurses_cursor_disable(nc);
	ui_draw_cmdline(ui);
	ui_showmenu(ui, NULL, 0);
}

void ui_cmdline_set(ui_t *ui, const char *line)
{
	if (cmdline_set(&ui->cmdline, line)) {
		request_draw_cmdline(ui);
	}
}

const char *ui_cmdline_get(ui_t *ui)
{
	return cmdline_get(&ui->cmdline);
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

void request_draw_cmdline(ui_t *ui)
{
	ui->needs_redraw_cmdline = true;
}

void ui_draw_cmdline(ui_t *ui)
{
	ui->needs_redraw_cmdline = false;
	char nums[16];
	char size[32];
	char mtime[32];
	const dir_t *dir;
	const file_t *file;

	ncplane_erase(ui->plane_cmdline);
	ncplane_set_bg_default(ui->plane_cmdline);
	ncplane_set_fg_default(ui->plane_cmdline);

	int rhs_sz = 0;
	int lhs_sz = 0;

	if (!cmdline_prefix_get(&ui->cmdline)) {
		if ((dir = nav_current_dir(ui->nav))) {
			if((file = dir_current_file(dir))) {
				/* TODO: for empty directories, show the stat of the
				 * directory instead (on 2021-07-18) */
				lhs_sz = ncplane_printf_yx(ui->plane_cmdline, 0, 0,
						"%s %2.ld %s %s %4s %s%s%s",
						perms(file->stat.st_mode), file->stat.st_nlink,
						owner(file->stat.st_uid), group(file->stat.st_gid),
						readable_fs(file->stat.st_size, size),
						print_time(file->stat.st_mtime, mtime, sizeof(mtime)),
						file->link_target ? " -> " : "",
						file->link_target ? file->link_target : "");
			}

			rhs_sz = snprintf(nums, sizeof(nums), " %d/%d", dir->len ? dir->ind + 1 : 0, dir->len);
			ncplane_putstr_yx(ui->plane_cmdline, 0, ui->ncol - rhs_sz, nums);

			if (dir->filter[0]) {
				rhs_sz += strlen(dir->filter) + 3;
				ncplane_set_bg_palindex(ui->plane_cmdline, COLOR_GREEN);
				ncplane_set_fg_palindex(ui->plane_cmdline, COLOR_BLACK);
				ncplane_printf_yx(ui->plane_cmdline, 0, ui->ncol-rhs_sz+1, " %s ", dir->filter);
				ncplane_set_bg_default(ui->plane_cmdline);
				ncplane_set_fg_default(ui->plane_cmdline);
				ncplane_putchar(ui->plane_cmdline, ' ');
			}
			if (cvector_size(ui->nav->load) > 0) {
				if (ui->nav->mode == MODE_COPY) {
					ncplane_set_channels(ui->plane_cmdline, cfg.colors.copy);
				} else {
					ncplane_set_channels(ui->plane_cmdline, cfg.colors.delete);
				}
				rhs_sz += int_sz(cvector_size(ui->nav->load)) + 3;
				ncplane_printf_yx(ui->plane_cmdline, 0, ui->ncol-rhs_sz+1, " %lu ", cvector_size(ui->nav->load));
				ncplane_set_bg_default(ui->plane_cmdline);
				ncplane_putchar(ui->plane_cmdline, ' ');
			}
			if (ui->nav->selection_len > 0) {
				ncplane_set_channels(ui->plane_cmdline, cfg.colors.selection);
				rhs_sz += int_sz(ui->nav->selection_len) + 3;
				ncplane_printf_yx(ui->plane_cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->nav->selection_len);
				ncplane_set_bg_default(ui->plane_cmdline);
				ncplane_putchar(ui->plane_cmdline, ' ');
			}
			if (lhs_sz + rhs_sz > ui->ncol) {
				ncplane_putwc_yx(ui->plane_cmdline, 0, ui->ncol - rhs_sz - 1, cfg.truncatechar);
				ncplane_putchar(ui->plane_cmdline, ' ');
			}
		}
	} else {
		const int cursor_pos = cmdline_print(&ui->cmdline, ui->plane_cmdline);
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

	const dir_t *dir;
	const file_t *file;

	ncplane_erase(ui->infoline);

	if (user[0] == 0) {
		getlogin_r(user, sizeof(user));
		gethostname(host, sizeof(host));
		home = getenv("HOME");
		home_len = strlen(home);
	}

	ncplane_set_styles(ui->infoline, NCSTYLE_BOLD);
	ncplane_set_fg_palindex(ui->infoline, COLOR_GREEN);
	ncplane_putstr_yx(ui->infoline, 0, 0, user);
	ncplane_putchar(ui->infoline, '@');
	ncplane_putstr(ui->infoline, host);
	ncplane_set_fg_default(ui->infoline);

	ncplane_set_styles(ui->infoline, NCSTYLE_NONE);
	ncplane_putchar(ui->infoline, ':');
	ncplane_set_styles(ui->infoline, NCSTYLE_BOLD);

	if ((dir = ui->nav->dirs[0])) {
		// shortening should work fine with ascii only names
		const char *end = dir->path + strlen(dir->path);
		int remaining;
		ncplane_cursor_yx(ui->infoline, NULL, &remaining);
		remaining = ui->ncol - remaining;
		if ((file = dir_current_file(dir))) {
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

/* wdraw_dir {{{ */

unsigned long ext_channel_find(const char *ext)
{
	size_t i;
	if (ext) {
		const size_t l = cvector_size(cfg.colors.ext_channels);
		for (i = 0; i < l; i++) {
			if (strcaseeq(ext, cfg.colors.ext_channels[i].ext)) {
				return cfg.colors.ext_channels[i].channel;
			}
		}
	}
	return 0;
}

static void print_file(struct ncplane *n, const file_t *file,
		bool iscurrent, char **sel, char **load, enum movemode_e mode,
		const char *highlight)
{
	int ncol, y0, x;
	char size[16];
	wchar_t buf[256];
	ncplane_dim_yx(n, NULL, &ncol);
	ncplane_cursor_yx(n, &y0, NULL);

	bool isdir, islink;
	if ((isdir = file_isdir(file))) {
		snprintf(size, sizeof(size), "%d", file->filecount);
	} else {
		readable_fs(file->stat.st_size, size);
	}

	int rightmargin = strlen(size) + 2;
	if ((islink = file_islink(file))) {
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
	if (highlight && (hlsubstr = strcasestr(file->name, highlight))) {
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

static void wdraw_dir(struct ncplane *n, dir_t *dir, char **sel, char **load,
		enum movemode_e mode, const char *highlight)
{
	int nrow, i, offset;

	ncplane_erase(n);
	ncplane_dim_yx(n, &nrow, NULL);
	ncplane_cursor_move_yx(n, 0, 0);

	if (dir) {
		if (dir->error) {
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

			const int l = min(dir->len - offset, nrow);
			for (i = 0; i < l; i++) {
				ncplane_cursor_move_yx(n, i, 0);
				print_file(n, dir->files[i + offset],
						i == dir->pos, sel, load, mode, highlight);
			}
		}
	}
}
/* }}} */

/* main drawing/echo/err {{{ */

void ui_request_draw(ui_t *ui)
{
	ui->needs_redraw = true;
}

void ui_draw_lazy(ui_t *ui)
{
	if (ui->needs_redraw) {
		ui_draw(ui);
	} else {
		if (ui->needs_redraw_cmdline) {
			ui_draw_cmdline(ui);
		}
		// redraw dirs here
	}
}

void ui_draw(ui_t *ui)
{
	ui->needs_redraw = false;
	ui_draw_dirs(ui);
	draw_menu(ui->menu, ui->menubuf);
	ui_draw_cmdline(ui);
}

/* to not overwrite errors */
void ui_draw_dirs(ui_t *ui)
{
	int i;
	dir_t *pdir;

	draw_info(ui);

	const int l = ui->nav->ndirs;
	for (i = 0; i < l; i++) {
		wdraw_dir(ui->wdirs[l-i-1],
				ui->nav->dirs[i],
				ui->nav->selection,
				ui->nav->load,
				ui->nav->mode,
				i == 0 && ui->highlight_active ? ui->highlight : NULL);
	}

	if (cfg.preview && ui->ndirs > 1) {
		if ((pdir = ui->nav->preview)) {
			wdraw_dir(wpreview(ui), ui->nav->preview, ui->nav->selection,
					ui->nav->load, ui->nav->mode, NULL);
		} else {
			update_preview(ui);
			ui_draw_preview(ui);
		}
	}
	notcurses_render(nc);
}

void ui_clear(ui_t *ui)
{
	/* infoline and dirs have to be cleared *and* rendered, otherwise they will
	 * bleed into the first row */
	ncplane_erase(ncstd);
	ncplane_erase(ui->infoline);
	for (int i = 0; i < ui->ndirs; i++) {
		ncplane_erase(ui->wdirs[i]);
	}
	ncplane_erase(ui->plane_cmdline);

	notcurses_render(nc);

	notcurses_refresh(nc, NULL, NULL);

	notcurses_cursor_enable(nc, 0, 0);
	notcurses_cursor_disable(nc);

	ui_request_draw(ui);
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

	if (init) {
		ncplane_erase(ui->plane_cmdline);
		ncplane_set_fg_palindex(ui->plane_cmdline, COLOR_RED);
		ncplane_putstr_yx(ui->plane_cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->plane_cmdline);
		notcurses_render(nc);
	}
}

void ui_vechom(ui_t *ui, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	cvector_push_back(ui->messages, msg);

	if (init) {
		ncplane_erase(ui->plane_cmdline);
		ncplane_set_fg_palindex(ui->plane_cmdline, 15);
		ncplane_putstr_yx(ui->plane_cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->plane_cmdline);
		notcurses_render(nc);
	}
}

/* }}} */

/* history {{{ */

void ui_history_append(ui_t *ui, const char *line)
{
	history_append(&ui->history, line);
}

const char *ui_history_prev(ui_t *ui)
{
	return history_prev(&ui->history);
}

const char *ui_history_next(ui_t *ui)
{
	return history_next(&ui->history);
}

/* }}} */

void ui_deinit(ui_t *ui)
{
	history_write(&ui->history, cfg.historypath);
	history_clear(&ui->history);
	cvector_ffree(ui->messages, free);
	cvector_ffree(ui->menubuf, free);
	cache_deinit(&ui->previewcache);
	free(ui->highlight);
	cmdline_deinit(&ui->cmdline);
	notcurses_stop(nc);
}
