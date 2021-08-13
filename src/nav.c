#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "app.h"
#include "async.h"
#include "config.h"
#include "dir.h"
#include "dirheap.h"
#include "log.h"
#include "nav.h"
#include "notify.h"
#include "util.h"

void nav_update_preview(nav_t *nav);

static dir_t *nav_load_dir(nav_t *nav, const char *path);
file_t *nav_current_file(const nav_t *nav);
/* static void nav_mark_save_current(nav_t *nav, char mark); */
static void update_watchers(nav_t *nav);
static void nav_remove_preview(nav_t *nav);
static void mark_save(nav_t *nav, char mark, const char *path);

static const char *concatpath(const char *dir, const char *file)
{
	static char path[PATH_MAX + 1];
	snprintf_nowarn(path, PATH_MAX, "%s%s%s", dir,
			streq(dir, "/") ? "" : "/", file);
	return path;
}

bool cvector_contains(const char *path, char **selection)
{
	char **it;
	for (it = cvector_begin(selection); it != cvector_end(selection);
			++it) {
		if (*it && streq(*it, path))
			return true;
	}
	return false;
}

static void populate(nav_t *nav)
{
	int i;

	const char *s;
	char pwd[PATH_MAX];
	if ((s = getenv("PWD"))) {
		strncpy(pwd, s, sizeof(pwd)-1);
	} else {
		getcwd(pwd, sizeof(pwd));
	}

	nav->dirs[0] = nav_load_dir(nav, pwd); /* current dir */
	dir_t *d = nav->dirs[0];
	for (i = 1; i < nav->ndirs; i++) {
		if ((s = dir_parent(d))) {
			d = nav_load_dir(nav, s);
			nav->dirs[i] = d;
			dir_sel(d, nav->dirs[i-1]->name);
		} else {
			nav->dirs[i] = NULL;
		}
	}
}

void nav_init(nav_t *nav)
{
	log_trace("init_nav");

	if (cfg.startpath) {
		if ((chdir(cfg.startpath)) != 0) {
			error("chdir: %s", strerror(errno));
		} else {
			setenv("PWD", cfg.startpath, true);
		}
	}

	nav->dircache.size = 0;
	nav->ndirs = 0;
	nav->dirs = NULL;
	nav->height = 0;
	nav->load_len = 0;
	nav->load = NULL;
	nav->marklist = NULL;
	nav->mode = 0;
	nav->prev_selection = NULL;
	nav->selection_len = 0;
	nav->selection = NULL;
	nav->visual = false;
	nav->preview = NULL;

	const int l = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
	cvector_grow(nav->dirs, l);
	nav->ndirs = l;

	populate(nav);

	update_watchers(nav);

	nav_update_preview(nav);
}

void nav_recol(nav_t *nav)
{
	int i;
	const int l = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);

	nav_remove_preview(nav);
	for (i = 0; i < nav->ndirs; i++) {
		dirheap_insert(&nav->dircache, nav->dirs[i]);
	}

	cvector_set_size(nav->dirs, l);
	nav->ndirs = l;

	populate(nav);

	update_watchers(nav);

	nav_update_preview(nav);
}

void nav_chdir(nav_t *nav, const char *path, bool save)
{
	log_trace("nav_chdir: %s", path);

	nav_selection_visual_stop(nav);

	char fullpath[PATH_MAX];
	if (path[0] != '/') {
		realpath(path, fullpath);
		path = fullpath;
	}

	if (chdir(path) != 0) {
		error("chdir: %s", strerror(errno));
		return;
	}

	setenv("PWD", path, true);

	if (save && !nav->dirs[0]->error) {
		mark_save(nav, '\'', nav->dirs[0]->path);
	}

	nav_remove_preview(nav);
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			dirheap_insert(&nav->dircache, nav->dirs[i]);
		}
	}
	populate(nav);
	nav_update_preview(nav);
	update_watchers(nav);
}

static void update_watchers(nav_t *nav)
{
	const int l = nav->ndirs;
	const char **w = malloc(sizeof(char *) * l);

	int i;
	for (i = 0; i < l; i++) {
		w[i] = nav->dirs[i] != NULL ? nav->dirs[i]->path : NULL;
	}

	notify_set_watchers(w, l);

	free(w);
}

void nav_sort(nav_t *nav)
{
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			nav->dirs[i]->hidden = cfg.hidden;
			dir_sort(nav->dirs[i]);
		}
	}
	if (nav->preview) {
		dir_sort(nav->preview);
	}
}

void nav_hidden_set(nav_t *nav, bool hidden)
{
	log_trace("hidden_set %d", hidden);

	cfg.hidden = hidden;
	nav_sort(nav);
}

/* TODO: It actually makes more sense to update dir access times when leaving
 * the directory, but we would have to find the directory in the heap first.
 * For now, we update when inserting/updating the dir.
 * (on 2021-08-03) */
dir_t *nav_load_dir(nav_t *nav, const char *path)
{
	/* log_trace("nav_load_dir %s", path); */
	dir_t *dir;
	char fullpath[PATH_MAX];
	if (path[0] != '/') {
		realpath(path, fullpath);
		path = fullpath;
	}

	if ((dir = dirheap_take(&nav->dircache, path))) {
		if (!dir_check(dir)) {
			async_load_dir(dir->path);
		}
		dir->hidden = cfg.hidden;
		dir_sort(dir);
	} else {
		dir = new_loading_dir(path);
		async_load_dir(path);
	}
	return dir;
}

file_t *nav_current_file(const nav_t *nav)
{
	return dir_current_file(nav->dirs[0]);
}

dir_t *nav_current_dir(const nav_t *nav) { return nav->dirs[0]; }

dir_t *nav_preview_dir(const nav_t *nav) { return nav->preview; }


void copy_attrs(dir_t *dir, dir_t *olddir) {
	strncpy(dir->filter, olddir->filter, sizeof(dir->filter));
	dir->hidden = cfg.hidden;
	dir->pos = olddir->pos;
	/* dir->sorted = olddir->sorted && (dir->sorttype == olddir->sorttype); */
	dir->sorted = false;
	dir->sorttype = olddir->sorttype;
	dir->dirfirst = olddir->dirfirst;
	dir->reverse = olddir->reverse;
	dir->ind = olddir->ind;
	dir->access = olddir->access;
	dir_sort(dir);

	if (olddir->sel) {
		dir_sel(dir, olddir->sel);
		free(olddir->sel);
		olddir->sel = NULL;
	} else if (olddir->ind < olddir->len) {
		dir_sel(dir, olddir->files[olddir->ind]->name);
	}
}

/* TODO: compare load times in case of another thread being
 * faster (on 2021-07-28) */
bool nav_insert_dir(nav_t *nav, dir_t *dir)
{
	/* log_debug("nav_insert_dir %s %p %d", dir->path, dir, dir->len); */
	dir_t **dirptr = dirheap_find(&nav->dircache, dir->path);
	bool ret = false;
	if (dirptr) {
		copy_attrs(dir, *dirptr);
		free_dir(*dirptr);
		*dirptr = dir;
		/* TODO: maybe don't update here (on 2021-08-09) */
		dirheap_pupdate(&nav->dircache, dirptr, time(NULL));
	} else {
		int i;
		for (i = 0; i < nav->ndirs; i++) {
			if (nav->dirs[i] && streq(nav->dirs[i]->path, dir->path)) {
				copy_attrs(dir, nav->dirs[i]);
				free(nav->dirs[i]);
				nav->dirs[i] = dir;
				ret = true;
				break;
			}
		}
		if (nav->preview && streq(nav->preview->path, dir->path)) {
			copy_attrs(dir, nav->preview);
			free(nav->preview);
			nav->preview = dir;
			ret = true;
		}
		if (!ret) {
			dirheap_insert(&nav->dircache, dir);
		}
	}
	if (ret) {
		nav_update_preview(nav);
	}
	return ret;
}

void nav_check_dirs(const nav_t *nav)
{
	/* log_trace("check_dirs"); */

	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (!nav->dirs[i]) {
			continue;
		}
		if (!dir_check(nav->dirs[i])) {
			async_load_dir(nav->dirs[i]->path);
		}
	}
	if (nav->preview && !dir_check(nav->preview)) {
		async_load_dir(nav->preview->path);
	}
}

void nav_drop_cache(nav_t *nav)
{
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			free_dir(nav->dirs[i]);
		}
	}
	nav_remove_preview(nav);

	for (i = 0; i < nav->dircache.size; i++) {
		free_dir(nav->dircache.dirs[i]);
	}
	nav->dircache.size = 0;

	char path[PATH_MAX];
	const char *s;
	if ((s = getenv("PWD"))) {
		strncpy(path, s, sizeof(path)-1);
	} else {
		getcwd(path, sizeof(path));
	}

	populate(nav);
	nav_update_preview(nav);
	update_watchers(nav);
}

static void nav_remove_preview(nav_t *nav)
{
	if (nav->preview) {
		notify_remove_watcher(nav->preview->path);
		dirheap_insert(&nav->dircache, nav->preview);
		nav->preview = NULL;
	}
}

void nav_update_preview(nav_t *nav)
{
	/* log_trace("update_preview %s", nav_current_dir(nav)->path); */
	if (!cfg.preview) {
		nav_remove_preview(nav);
		return;
	}

	file_t *file = nav_current_file(nav);
	dir_t *pdir = nav->preview;
	if (file && file_isdir(file)) {
		const char *p = concatpath(nav->dirs[0]->path, file->name);
		if (pdir && streq(pdir->path, p)) {
			return;
		} else {
			dir_t *dir = nav_load_dir(nav, p);
			if (dir) {
				notify_add_watcher(dir->path);
			}

			/* TODO: actually we need to check if the previous preview is
			 * any of the active dirs now (on 2021-08-07) */
			if (pdir && pdir != nav->dirs[0]) {
				notify_remove_watcher(pdir->path);
				dirheap_insert(&nav->dircache, pdir);
			}

			nav->preview = dir;
		}
	} else {
		// file preview incoming
		if (pdir) {
			/* TODO: as above (on 2021-08-07) */
			if (pdir != nav->dirs[0]) {
				notify_remove_watcher(pdir->path);
				dirheap_insert(&nav->dircache, pdir);
			}
		}
		nav->preview = NULL;
	}
}

/* selection {{{ */
void nav_selection_clear(nav_t *nav)
{
	cvector_fclear(nav->selection, free);
	nav->selection_len = 0;
}

void selection_add_file(nav_t *nav, const char *path)
{
	/* log_trace("selection_add_file"); */
	char **it;
	for (it = cvector_begin(nav->selection);
			it != cvector_end(nav->selection); ++it) {
		if (!*it) {
			continue;
		}
		if (streq(*it, path)) {
			return;
		}
	}
	cvector_push_back(nav->selection, strdup(path));
	nav->selection_len++;
}

void selection_toggle_file(nav_t *nav, const char *path)
{
	/* log_trace("toggle_selection"); */
	char **it;
	for (it = cvector_begin(nav->selection);
			it != cvector_end(nav->selection); ++it) {
		if (!*it) {
			continue;
		}
		if (streq(*it, path)) {
			free(*it);
			*it = NULL;
			nav->selection_len--;
			if (nav->selection_len == 0) {
				cvector_set_size(nav->selection, 0);
			}
			return;
		}
	}
	cvector_push_back(nav->selection, strdup(path));
	nav->selection_len++;
}

void nav_selection_toggle_current(nav_t *nav)
{
	if (!nav->visual) {
		const file_t *f = nav_current_file(nav);
		if (f) {
			selection_toggle_file(nav, f->path);
		}
	}
}

void nav_selection_reverse(nav_t *nav)
{
	const dir_t *dir = nav->dirs[0];
	for (int i = 0; i < dir->len; i++) {
		selection_toggle_file(nav, dir->files[i]->path);
	}
}

void nav_selection_visual_start(nav_t *nav)
{
	if (nav->visual) {
		return;
	}
	dir_t *dir = nav->dirs[0];
	if (!dir) {
		return;
	}
	if (dir->len == 0) {
		return;
	}
	nav->visual = true;
	nav->visual_anchor = dir->ind;
	selection_add_file(nav, dir->files[dir->ind]->path);
	char **it;
	for (it = cvector_begin(nav->selection);
			it != cvector_end(nav->selection); ++it) {
		if (!*it) {
			continue;
		}
		cvector_push_back(nav->prev_selection, *it);
	}
}

void nav_selection_visual_stop(nav_t *nav)
{
	if (!nav->visual) {
		return;
	}
	nav->visual = false;
	nav->visual_anchor = 0;
	/* we dont free anything here because the old selection is always a subset of the
	 * new slection */
	cvector_set_size(nav->prev_selection, 0);
}

void nav_selection_visual_toggle(nav_t *nav)
{
	if (nav->visual) {
		nav_selection_visual_stop(nav);
	} else {
		nav_selection_visual_start(nav);
	}
}

void selection_visual_update(nav_t *nav, int origin, int from, int to)
{
	/* TODO: this should be easier (on 2021-07-25) */
	int hi, lo;
	hi = lo = origin;
	if (from >= origin) {
		if (to > from) {
			lo = from + 1;
			hi = to;
		} else if (to < origin) {
			hi = from;
			lo = to;
		} else {
			hi = from;
			lo = to + 1;
		}
	}
	if (from < origin) {
		if (to < from) {
			lo = to;
			hi = from - 1;
		} else if (to > origin) {
			lo = from;
			hi = to;
		} else {
			lo = from;
			hi = to - 1;
		}
	}
	dir_t *dir = nav->dirs[0];
	for (; lo <= hi; lo++) {
		/* never unselect the old selection */
		if (!cvector_contains(dir->files[lo]->path,
					nav->prev_selection)) {
			selection_toggle_file(nav, dir->files[lo]->path);
		}
	}
}

void nav_selection_write(const nav_t *nav, const char *path)
{
	log_trace("nav_selection_write");

	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(dir);

	FILE *fp = fopen(path, "w");
	if (!fp) {
		error("selfile: %s", strerror(errno));
		return;
	}

	if (nav->selection_len > 0) {
		char **it;
		for (it = cvector_begin(nav->selection);
				it != cvector_end(nav->selection); ++it) {
			if (!*it) {
				continue;
			}
			fputs(*it, fp);
			fputc('\n', fp);
		}
	} else {
		file_t *f = nav_current_file(nav);
		if (f) {
			fputs(f->path, fp);
			fputc('\n', fp);
		}
	}
	fclose(fp);
}

/* }}} */

/* navigation {{{ */
static bool nav_move(nav_t *nav, int ct)
{
	dir_t *dir = nav->dirs[0];
	const int cur = dir->ind;

	dir->ind = max(min(dir->ind + ct, dir->len - 1), 0);
	if (ct < 0) {
		dir->pos = min(max(cfg.scrolloff, dir->pos + ct), dir->ind);
	} else {
		dir->pos = max(min(nav->height - 1 - cfg.scrolloff, dir->pos + ct), nav->height - dir->len + dir->ind);
	}

	if (dir->ind != cur) {
		if (nav->visual) {
			selection_visual_update(nav, nav->visual_anchor, cur,
					dir->ind);
		}

		nav_update_preview(nav);
		return true;
	}
	/* We actually have to redraw becuase we could be selecting the last
	 * file in the directory. */
	return dir->ind == dir->len - 1;
}

bool nav_up(nav_t *nav, int ct) { return nav_move(nav, -ct); }

bool nav_down(nav_t *nav, int ct) { return nav_move(nav, ct); }

bool nav_top(nav_t *nav) { return nav_up(nav, nav->dirs[0]->ind); }

bool nav_bot(nav_t *nav)
{
	return nav_down(nav, nav->dirs[0]->len - nav->dirs[0]->ind);
}

void nav_sel(nav_t *nav, const char *filename)
{
	/* log_trace("nav_sel %s", filename); */
	dir_sel(nav->dirs[0], filename);
	nav_update_preview(nav);
}

file_t *nav_open(nav_t *nav)
{
	log_trace("nav_open");
	file_t *file = nav_current_file(nav);
	if (!file) {
		return NULL;
	}
	nav_selection_visual_stop(nav); /* before or after chdir? */
	if (!file_isdir(file)) {
		return file;
	}
	nav_chdir(nav, concatpath(nav->dirs[0]->path, file->name), false);
	return NULL;
}

void nav_updir(nav_t *nav)
{
	if (streq(nav->dirs[0]->path, "/")) {
		return;
	}
	const char *current_name = nav->dirs[0]->name;
	nav_chdir(nav, dir_parent(nav->dirs[0]), false);
	nav_sel(nav, current_name);
	nav_update_preview(nav);
}

/* }}} */

/* marks {{{ */
static void mark_save(nav_t *nav, char mark, const char *path)
{
	log_trace("mark_save %c %s", mark, path);
	mark_t *it;
	for (it = cvector_begin(nav->marklist);
			it != cvector_end(nav->marklist); ++it) {
		if (it->mark == mark) {
			if (!streq(it->path, path)) {
				it->path =
					realloc(it->path,
							sizeof(char) * (strlen(path) + 1));
				strcpy(it->path, path);
			}
			return;
		}
	}
	mark_t newmark = {.mark = mark, .path = strdup(path)};
	cvector_push_back(nav->marklist, newmark);
}

/* static void nav_mark_save_current(nav_t *nav, char mark) */
/* { */
/* 	mark_save(nav, mark, nav->dirs[0]->path); */
/* } */

bool nav_mark_load(nav_t *nav, char mark)
{
	log_trace("nav_mark_load: %c", mark);
	mark_t *it;
	for (it = cvector_begin(nav->marklist);
			it != cvector_end(nav->marklist); ++it) {
		if (it->mark == mark) {
			if (streq(it->path, nav->dirs[0]->path)) {
				log_info("mark is current dir: %c", mark);
			} else {
				nav_chdir(nav, it->path, true);
			}
			/* TODO: shouldn't return true if chdir fails (on
			 * 2021-07-22) */
			return 1;
		}
	}
	error("no such mark: %c", mark);
	return 0;
}
/* }}} */

/* load/copy/move {{{ */

/* TODO: Make it possible to append to cut/copy buffer (on 2021-07-25) */
void nav_load_files(nav_t *nav, int mode)
{
	nav_selection_visual_stop(nav);
	nav->mode = mode;
	if (nav->selection_len == 0) {
		nav_selection_toggle_current(nav);
	}
	nav_clear_load(nav);
	char **tmp = nav->load;
	nav->load = nav->selection;
	nav->load_len = nav->selection_len;
	nav->selection = tmp;
	nav->selection_len = 0;
}

void nav_clear_load(nav_t *nav)
{
	cvector_fclear(nav->load, free);
	nav->load_len = 0;
}

char * const *nav_get_load(const nav_t *nav) { return nav->load; }

int nav_get_mode(const nav_t *nav) { return nav->mode; }

void nav_cut(nav_t *nav) { nav_load_files(nav, MODE_MOVE); }

void nav_copy(nav_t *nav) { nav_load_files(nav, MODE_COPY); }
/* }}} */

/* filter {{{ */
void nav_filter(nav_t *nav, const char *filter)
{
	dir_t *d;
	file_t *f;

	d = nav->dirs[0];
	f = dir_current_file(d);
	dir_filter(d, filter);
	dir_sel(d, f ? f->name : NULL);
}

const char *nav_filter_get(const nav_t *nav) { return nav->dirs[0]->filter; }
/* }}} */

#ifndef free_mark
#define free_mark(mark) free((mark).path)
#endif

void nav_destroy(nav_t *nav)
{
	int i;
	cvector_ffree(nav->selection, free);
	/* prev_selection _never_ holds allocated paths that are not already
	 * free'd in nav->selection */
	cvector_free(nav->prev_selection);
	cvector_ffree(nav->load, free);
	for (i = 0; i < nav->dircache.size; i++) {
		free_dir(nav->dircache.dirs[i]);
	}
	cvector_ffree(nav->marklist, free_mark);
}
