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
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "nav.h"
#include "preview.h"
#include "ui.h"
#include "util.h"

#define TRACE 1

static bool init = false;

inline static struct ncplane *wpreview(ui_t *ui)
{
	return ui->wdirs[ui->ndirs-1];
}

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
	if (!(nc = notcurses_core_init(&opts, NULL))) {
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
	ui->nav = nav;

	ncsetup();
	ui->input_ready_fd = notcurses_inputready_fd(nc);
	ui->nc = nc;
	ncplane_dim_yx(ncstd, &ui->nrow, &ui->ncol);
	nav->height = ui->nrow - 2;

	ui->wdirs = NULL;
	ui->ndirs = 0;

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

	/* ui->messages = NULL; */

	ui->highlight = NULL;
	ui->search_forward = true;

	ui->nav->load_len = 0;

	init = true;
	log_info("initialized ui");
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
	/* if (ui->file_preview) { */
	/* 	preview_free(ui->file_preview); */
	/* 	ui->file_preview = NULL; */
	/* } */
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

	int ncol, nrow;
	preview_t *pv;

	struct ncplane *w = wpreview(ui);
	ncplane_dim_yx(w, &nrow, &ncol);

	if ((pv = previewheap_take(&ui->previews, file->path))) {
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
	/* log_trace("ui_draw_preview"); */
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
				previewheap_insert(&ui->previews, ui->file_preview);
				ui->file_preview = ui_load_preview(ui, file);
			}
		} else {
			ui->file_preview = ui_load_preview(ui, file);
		}
	}
}

static void wansi_matchattr(struct ncplane *w, int a)
{
	/* log_debug("match_attr %d", a); */
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

static void wdraw_file_preview(struct ncplane *n, preview_t *pv)
{
#ifdef TRACE
	log_trace("wdraw_preview");
#endif

	int i, nrow;

	ncplane_erase(n);
	/* render, otherwise artefacts of the previous preview remain
	 * as of 2021-08-19 */
	notcurses_render(nc);

	if (pv) {
		ncplane_dim_yx(n, &nrow, NULL);
		ncplane_set_styles(n, NCSTYLE_NONE);
		ncplane_set_fg_default(n);
		ncplane_set_bg_default(n);

		const int l = cvector_size(pv->lines);
		log_debug("%s %d", pv->path, l);
		for (i = 0; i < l && i < nrow; i++) {
			ncplane_cursor_move_yx(n, i, 0);
			wansi_addstr(n, pv->lines[i]);
		}
	}

	notcurses_render(nc);
}

bool ui_insert_preview(ui_t *ui, preview_t *pv)
{
	/* log_debug("ui_insert_preview %s %d", pv->path, pv->nrow); */
	preview_t **pvptr;
	const file_t *file;

	if ((file = nav_current_file(ui->nav)) && (file == pv->fptr || streq(pv->path, file->path))) {
		preview_free(ui->file_preview);
		ui->file_preview = pv;
		return true;
	} else {
		if ((pvptr = previewheap_find(&ui->previews, pv->path))) {
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
	return false;
}


/* }}} */

/* search {{{ */
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
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	const int l = strlen(ui->cmd_acc_left);
	if (l > 0) {
		ui->cmd_acc_left[l - 1] = 0;
		log_debug("cmd_delete: %s", ui->cmd_acc_left);
	}
	draw_cmdline(ui);
}

/* pass a ct argument to move over words? */
void ui_cmd_left(ui_t *ui)
{
	log_trace("cmd_left");
	int j;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	const int l = strlen(ui->cmd_acc_left);
	if (l > 0) {
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
	int i;
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	const int l = strlen(ui->cmd_acc_right);
	const int j = strlen(ui->cmd_acc_left);
	if (j > 0) {
		if (l < ACC_SIZE - 2) {
			ui->cmd_acc_left[l] = ui->cmd_acc_right[0];
			ui->cmd_acc_left[l + 1] = 0;
			for (i = 0; i < j; i++) {
				ui->cmd_acc_right[i] = ui->cmd_acc_right[i + 1];
			}
		}
	}
	draw_cmdline(ui);
}

void ui_cmd_home(ui_t *ui)
{
	log_trace("cmd_home");
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	const int l = strlen(ui->cmd_acc_left);
	if (l > 0) {
		int j = min(strlen(ui->cmd_acc_right) - 1 + l, ACC_SIZE - 1 - l);
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
	if (ui->cmd_prefix[0] == 0) {
		return;
	}
	if (ui->cmd_acc_right[0] != 0) {
		const int j = strlen(ui->cmd_acc_left);
		const int l = ACC_SIZE - 1 - strlen(ui->cmd_acc_left);
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
	char nums[16];
	char size[32];
	char mtime[32];
	const dir_t *dir;
	const file_t *file;

	ncplane_erase(ui->cmdline);
	ncplane_set_bg_default(ui->cmdline);
	ncplane_set_fg_default(ui->cmdline);

	int rhs_sz = 0;
	int lhs_sz = 0;

	if (ui->cmd_prefix[0] == 0) {
		if ((dir = nav_current_dir(ui->nav))) {
			if((file = dir_current_file(dir))) {
				/* TODO: for empty directories, show the stat of the
				 * directory instead (on 2021-07-18) */
				lhs_sz = ncplane_printf_yx(ui->cmdline, 0, 0,
						"%s %2.ld %s %s %4s %s%s%s",
						perms(file->stat.st_mode), file->stat.st_nlink,
						owner(file->stat.st_uid), group(file->stat.st_gid),
						readable_fs(file->stat.st_size, size),
						print_time(file->stat.st_mtime, mtime, sizeof(mtime)),
						file->link_target ? " -> " : "",
						file->link_target ? file->link_target : "");
			}

			rhs_sz = snprintf(nums, sizeof(nums), " %d/%d", dir->len ? dir->ind + 1 : 0, dir->len);
			ncplane_putstr_yx(ui->cmdline, 0, ui->ncol - rhs_sz, nums);

			if (dir->filter[0]) {
				rhs_sz += strlen(dir->filter) + 3;
				ncplane_set_bg_palindex(ui->cmdline, COLOR_GREEN);
				ncplane_set_fg_palindex(ui->cmdline, COLOR_BLACK);
				ncplane_printf_yx(ui->cmdline, 0, ui->ncol-rhs_sz+1, " %s ", dir->filter);
				ncplane_set_bg_default(ui->cmdline);
				ncplane_set_fg_default(ui->cmdline);
				ncplane_putchar(ui->cmdline, ' ');
			}
			if (ui->nav->load_len > 0) {
				if (ui->nav->mode == MODE_COPY) {
					ncplane_set_channels(ui->cmdline, cfg.colors.copy);
				} else {
					ncplane_set_channels(ui->cmdline, cfg.colors.delete);
				}
				rhs_sz += int_sz(ui->nav->load_len) + 3;
				ncplane_printf_yx(ui->cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->nav->load_len);
				ncplane_set_bg_default(ui->cmdline);
				ncplane_putchar(ui->cmdline, ' ');
			}
			if (ui->nav->selection_len > 0) {
				ncplane_set_channels(ui->cmdline, cfg.colors.selection);
				rhs_sz += int_sz(ui->nav->selection_len) + 3;
				ncplane_printf_yx(ui->cmdline, 0, ui->ncol-rhs_sz+1, " %d ", ui->nav->selection_len);
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

/* history {{{ */

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */

void history_write(ui_t *ui)
{
	log_trace("history_write");
	FILE *fp;

	char *dir, *buf = strdup(cfg.historypath);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	if (!(fp = fopen(cfg.historypath, "w"))) {
		ui_error(ui, "history: %s", strerror(errno));
		return;
	}

	size_t i;
	for (i = 0; i < cvector_size(ui->history); i++) {
		fputs(ui->history[i], fp);
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
	wchar_t buf[128];
	ncplane_dim_yx(n, NULL, &ncol);
	ncplane_cursor_yx(n, &y0, NULL);

	/* log_debug("%s %u %u", file->name, ncplane_fg_rgb(n), ncplane_bg_rgb(n)); */

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
			ncplane_set_fg_default(n);
		}
	}

	if (iscurrent) {
		ncplane_set_bg_palindex(n, cfg.colors.current);
	}

	char *hlsubstr;
	ncplane_putchar(n, ' ');
	if (highlight && (hlsubstr = strcasestr(file->name, highlight))) {
		const int l = hlsubstr - file->name;
		const unsigned long ch = ncplane_channels(n);
		ncplane_putnstr(n, l, file->name);
		ncplane_set_channels(n, cfg.colors.search);
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
void ui_draw(ui_t *ui)
{
	ui_draw_dirs(ui);
	draw_menu(ui->menu, ui->menubuf);
	draw_cmdline(ui);
}

/* to not overwrite errors */
void ui_draw_dirs(ui_t *ui)
{
#ifdef TRACE
	log_trace("ui_draw_dirs");
#endif
	int i;
	dir_t *pdir;

	draw_info(ui);

	const int l = ui->nav->ndirs;
	for (i = 0; i < l; i++) {
		wdraw_dir(ui->wdirs[l-i-1], ui->nav->dirs[i], ui->nav->selection, ui->nav->load,
				ui->nav->mode, i == 0 ? ui->highlight : NULL);
	}

	if (cfg.preview) {
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

	ui_draw(ui);
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
		ncplane_erase(ui->cmdline);
		ncplane_set_fg_palindex(ui->cmdline, COLOR_RED);
		ncplane_putstr_yx(ui->cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->cmdline);
		notcurses_render(nc);
	}
}

void ui_vechom(ui_t *ui, const char *format, va_list args)
{
	char *msg;
	vasprintf(&msg, format, args);

	cvector_push_back(ui->messages, msg);

	if (init) {
		ncplane_erase(ui->cmdline);
		ncplane_set_fg_palindex(ui->cmdline, 15);
		ncplane_putstr_yx(ui->cmdline, 0, 0, msg);
		ncplane_set_fg_default(ui->cmdline);
		notcurses_render(nc);
	}
}

/* }}} */

void ui_destroy(ui_t *ui)
{
	int i;
	history_write(ui);
	history_clear(ui);
	cvector_free(ui->history);
	cvector_ffree(ui->messages, free);
	cvector_ffree(ui->menubuf, free);
	for (i = 0; i < ui->previews.size; i++) {
		preview_free(ui->previews.previews[i]);
	}
	free(ui->highlight);
	notcurses_stop(nc);
}
