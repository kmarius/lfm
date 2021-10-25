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
#include "cache.h"
#include "lualfm.h"
#include "log.h"
#include "nav.h"
#include "notify.h"
#include "util.h"

#define DIRCACHE_SIZE 31

void nav_update_preview(nav_t *nav);

static dir_t *load_dir(nav_t *nav, const char *path);
static void update_watchers(nav_t *nav);
static void remove_preview(nav_t *nav);
static void mark_save(nav_t *nav, char mark, const char *path);

static const char *concatpath(const char *dir, const char *file)
{
	static char path[PATH_MAX + 1];
	snprintf_nowarn(path, sizeof(path)-1, "%s%s%s", dir,
			streq(dir, "/") ? "" : "/", file);
	return path;
}

bool cvector_contains(const char *path, cvector_vector_type(char*) selection)
{
	size_t i;
	for (i = 0; i < cvector_size(selection); i++) {
		if (selection[i] && streq(selection[i], path))
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

	nav->dirs[0] = load_dir(nav, pwd); /* current dir */
	dir_t *d = nav->dirs[0];
	for (i = 1; i < nav->ndirs; i++) {
		if ((s = dir_parent(d))) {
			d = load_dir(nav, s);
			nav->dirs[i] = d;
			dir_sel(d, nav->dirs[i-1]->name);
		} else {
			nav->dirs[i] = NULL;
		}
	}
}

void nav_init(nav_t *nav)
{
	if (cfg.startpath) {
		if ((chdir(cfg.startpath)) != 0) {
			error("chdir: %s", strerror(errno));
		} else {
			setenv("PWD", cfg.startpath, true);
		}
	}

	cache_init(&nav->dircache, DIRCACHE_SIZE, (void (*)(void*)) dir_free);

	nav->ndirs = 0;
	nav->dirs = NULL;
	nav->height = 0;
	nav->load = NULL;
	nav->marklist = NULL;
	nav->mode = MODE_COPY;
	nav->prev_selection = NULL;
	nav->selection_len = 0;
	nav->selection = NULL;
	nav->visual = false;
	nav->preview = NULL;

	nav->ndirs = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
	cvector_grow(nav->dirs, nav->ndirs);

	populate(nav);

	update_watchers(nav);

	if (cfg.startfile) {
		nav_sel(nav, cfg.startfile);
	}

	nav_update_preview(nav);
}

void nav_recol(nav_t *nav)
{
	int i;
	/* We silently disable previews without changing cfg.preview.
	 * */
	const int l = max(1, cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0));

	remove_preview(nav);
	for (i = 0; i < nav->ndirs; i++) {
		cache_insert(&nav->dircache, nav->dirs[i], nav->dirs[i]->path);
	}

	cvector_set_size(nav->dirs, l);
	nav->ndirs = l;

	populate(nav);

	update_watchers(nav);

	nav_update_preview(nav);
}

bool nav_chdir(nav_t *nav, const char *path, bool save)
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
		return false;
	}

	notify_set_watchers(NULL, 0);

	setenv("PWD", path, true);

	if (save && !nav->dirs[0]->error) {
		mark_save(nav, '\'', nav->dirs[0]->path);
	}

	remove_preview(nav);
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			cache_insert(&nav->dircache, nav->dirs[i], nav->dirs[i]->path);
		}
	}
	populate(nav);
	update_watchers(nav);
	nav_update_preview(nav);

	return true;
}

static void update_watchers(nav_t *nav)
{
	const int l = nav->ndirs;
	const char *w[l];

	int i;
	for (i = 0; i < l; i++) {
		w[i] = nav->dirs[i] != NULL ? nav->dirs[i]->path : NULL;
	}

	notify_set_watchers(w, l);
}

void nav_sort(nav_t *nav)
{
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			nav->dirs[i]->hidden = cfg.hidden;
			/* TODO: maybe we can select the closest non-hidden file in case the
			 * current one will be hidden (on 2021-10-17) */
			if (nav->dirs[i]->len > 0) {
				const char *name = dir_current_file(nav->dirs[i])->name;
				dir_sort(nav->dirs[i]);
				dir_sel(nav->dirs[i], name);
			}
		}
	}
	if (nav->preview) {
		nav->preview->hidden = cfg.hidden;
		if (nav->preview->len > 0) {
			const char *name = dir_current_file(nav->preview)->name;
			dir_sort(nav->preview);
			dir_sel(nav->preview, name);
		}
	}
}

void nav_hidden_set(nav_t *nav, bool hidden)
{
	cfg.hidden = hidden;
	nav_sort(nav);
	nav_update_preview(nav);
}

/* TODO: It actually makes more sense to update dir access times when leaving
 * the directory, but we would have to find the directory in the heap first.
 * For now, we update when inserting/updating the dir.
 * (on 2021-08-03) */
static dir_t *load_dir(nav_t *nav, const char *path)
{
	dir_t *dir;
	char fullpath[PATH_MAX];
	if (path[0] != '/') {
		realpath(path, fullpath);
		path = fullpath;
	}

	if ((dir = cache_take(&nav->dircache, path))) {
		if (!dir_check(dir)) {
			async_dir_load(dir->path);
		}
		dir->hidden = cfg.hidden;
		dir_sort(dir);
	} else {
		dir = dir_new_loading(path);
		async_dir_load(path);
	}
	return dir;
}

file_t *nav_current_file(const nav_t *nav)
{
	return dir_current_file(nav->dirs[0]);
}

static void copy_attrs(dir_t *dir, dir_t *olddir) {
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
	dir_t *olddir;
	int i;

	if ((olddir = cache_take(&nav->dircache, dir->path))) {
		/* replace in dir cache */
		copy_attrs(dir, olddir);
		dir_free(olddir);
		cache_insert(&nav->dircache, dir, dir->path);
	} else {
		/* check if it an active directory */
		if (nav->preview && streq(nav->preview->path, dir->path)) {
			copy_attrs(dir, nav->preview);
			dir_free(nav->preview);
			nav->preview = dir;
			return true;
		} else {
			for (i = 0; i < nav->ndirs; i++) {
				if (nav->dirs[i] && streq(nav->dirs[i]->path, dir->path)) {
					copy_attrs(dir, nav->dirs[i]);
					dir_free(nav->dirs[i]);
					nav->dirs[i] = dir;
					if (i == 0) {
						/* current dir */
						nav_update_preview(nav);
					}
					return true;
				}
			}
		}
		dir_free(dir);
	}
	return false;
}

void nav_check_dirs(const nav_t *nav)
{
	int i;
	for (i = 0; i < nav->ndirs; i++) {
		if (!nav->dirs[i]) {
			continue;
		}
		if (!dir_check(nav->dirs[i])) {
			async_dir_load(nav->dirs[i]->path);
		}
	}
	if (nav->preview && !dir_check(nav->preview)) {
		async_dir_load(nav->preview->path);
	}
}

void nav_drop_cache(nav_t *nav)
{
	int i;

	for (i = 0; i < nav->ndirs; i++) {
		if (nav->dirs[i]) {
			dir_free(nav->dirs[i]);
		}
	}
	remove_preview(nav);

	cache_clear(&nav->dircache);

	populate(nav);
	nav_update_preview(nav);
	update_watchers(nav);
}

static void remove_preview(nav_t *nav)
{
	if (nav->preview) {
		notify_remove_watcher(nav->preview->path);
		cache_insert(&nav->dircache, nav->preview, nav->preview->path);
		nav->preview = NULL;
	}
}

void nav_update_preview(nav_t *nav)
{
	int i;
	if (!cfg.preview) {
		remove_preview(nav);
		return;
	}

	const file_t *file = nav_current_file(nav);
	if (file && file_isdir(file)) {
		if (nav->preview) {
			if (streq(nav->preview->path, file->path)) {
				return;
			}
			for (i = 0; i < nav->ndirs; i++) {
				if (nav->dirs[i] && streq(nav->preview->path, nav->dirs[i]->path)) {
					break;
				}
			}
			if (i == nav->ndirs) {
				notify_remove_watcher(nav->preview->path);
				cache_insert(&nav->dircache, nav->preview, nav->preview->path);
			}
		}
		nav->preview = load_dir(nav, file->path);
		notify_add_watcher(nav->preview->path);
	} else {
		// file preview or empty
		if (nav->preview) {
			for (i = 0; i < nav->ndirs; i++) {
				if (nav->dirs[i] && streq(nav->preview->path, nav->dirs[i]->path)) {
					break;
				}
			}
			if (i == nav->ndirs) {
				notify_remove_watcher(nav->preview->path);
				cache_insert(&nav->dircache, nav->preview, nav->preview->path);
			}
			nav->preview = NULL;
		}
	}
}

/* selection {{{ */
void nav_selection_clear(nav_t *nav)
{
	cvector_fclear(nav->selection, free);
	nav->selection_len = 0;
}

void nav_selection_add_file(nav_t *nav, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(nav->selection); i++) {
		if (!nav->selection[i]) {
			continue;
		}
		if (streq(nav->selection[i], path)) {
			return;
		}
	}
	cvector_push_back(nav->selection, strdup(path));
	nav->selection_len++;
}

void nav_selection_set(nav_t *nav, cvector_vector_type(char*) selection)
{
	nav_selection_clear(nav);
	cvector_free(nav->selection);
	nav->selection = selection;
}

void selection_toggle_file(nav_t *nav, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(nav->selection); i++) {
		if (!nav->selection[i]) {
			continue;
		}
		if (streq(nav->selection[i], path)) {
			free(nav->selection[i]);
			nav->selection[i] = NULL;
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
	file_t *file;
	if (!nav->visual) {
		if ((file = nav_current_file(nav))) {
			selection_toggle_file(nav, file->path);
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
	size_t i;
	dir_t *dir;
	if (nav->visual) {
		return;
	}
	if (!(dir = nav->dirs[0])) {
		return;
	}
	if (dir->len == 0) {
		return;
	}
	nav->visual = true;
	nav->visual_anchor = dir->ind;
	nav_selection_add_file(nav, dir->files[dir->ind]->path);
	for (i = 0; i < cvector_size(nav->selection); i++) {
		if (!nav->selection[i]) {
			continue;
		}
		cvector_push_back(nav->prev_selection, nav->selection[i]);
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
	const dir_t *dir = nav->dirs[0];
	for (; lo <= hi; lo++) {
		/* never unselect the old selection */
		if (!cvector_contains(dir->files[lo]->path, nav->prev_selection)) {
			selection_toggle_file(nav, dir->files[lo]->path);
		}
	}
}

void nav_selection_write(const nav_t *nav, const char *path)
{
	FILE *fp;
	file_t *f;

	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(dir);

	if (!(fp = fopen(path, "w"))) {
		error("selfile: %s", strerror(errno));
		return;
	}

	if (nav->selection_len > 0) {
		size_t i;
		for (i = 0; i< cvector_size(nav->selection); i++) {
			if (!nav->selection[i]) {
				continue;
			}
			fputs(nav->selection[i], fp);
			fputc('\n', fp);
		}
	} else {
		if ((f = nav_current_file(nav))) {
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
	 * file in the directory but not actually moving. */
	return dir->ind == dir->len - 1;
}

bool nav_up(nav_t *nav, int ct) { return nav_move(nav, -ct); }

bool nav_down(nav_t *nav, int ct) { return nav_move(nav, ct); }

bool nav_top(nav_t *nav) { return nav_up(nav, nav->dirs[0]->ind); }

bool nav_bot(nav_t *nav)
{
	return nav_down(nav, nav->dirs[0]->len - nav->dirs[0]->ind);
}

void nav_sel(nav_t *nav, const char *name)
{
	dir_sel(nav->dirs[0], name);
	nav_update_preview(nav);
}

file_t *nav_open(nav_t *nav)
{
	file_t *file;

	if (!(file = nav_current_file(nav))) {
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
	if (dir_isroot(nav->dirs[0])) {
		return;
	}
	const char *name = nav->dirs[0]->name;
	nav_chdir(nav, dir_parent(nav->dirs[0]), false);
	nav_sel(nav, name);
	nav_update_preview(nav);
}

/* }}} */

/* marks {{{ */
static void mark_save(nav_t *nav, char mark, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(nav->marklist); i++) {
		if (nav->marklist[i].mark == mark) {
			if (!streq(nav->marklist[i].path, path)) {
				nav->marklist[i].path =
					realloc(nav->marklist[i].path,
							sizeof(char) * (strlen(path) + 1));
				strcpy(nav->marklist[i].path, path);
			}
			return;
		}
	}
	mark_t newmark = {.mark = mark, .path = strdup(path)};
	cvector_push_back(nav->marklist, newmark);
}

bool nav_mark_load(nav_t *nav, char mark)
{
	size_t i;
	for (i = 0; i < cvector_size(nav->marklist); i++) {
		if (nav->marklist[i].mark == mark) {
			if (streq(nav->marklist[i].path, nav->dirs[0]->path)) {
				log_info("mark is current dir: %c", mark);
			} else {
				nav_chdir(nav, nav->marklist[i].path, true);
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

static void cvector_unsparse(cvector_vector_type(char *) vec)
{
	size_t i, j;
	for (i = j = 0; i < cvector_size(vec); i++) {
		if (vec[i]) {
			vec[j++] = vec[i];
		}
	}
	cvector_set_size(vec, j);
}

/* TODO: Make it possible to append to cut/copy buffer (on 2021-07-25) */
void nav_load_files(nav_t *nav, enum movemode_e mode)
{
	nav_selection_visual_stop(nav);
	nav->mode = mode;
	if (nav->selection_len == 0) {
		nav_selection_toggle_current(nav);
	}
	nav_load_clear(nav);
	char **tmp = nav->load;
	nav->load = nav->selection;
	cvector_unsparse(nav->load);
	nav->selection = tmp;
	nav->selection_len = 0;
}

void nav_load_clear(nav_t *nav)
{
	cvector_fclear(nav->load, free);
}

char * const *nav_get_load(const nav_t *nav) { return nav->load; }

enum movemode_e nav_get_mode(const nav_t *nav) { return nav->mode; }

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
	nav_update_preview(nav);
}

const char *nav_filter_get(const nav_t *nav) { return nav->dirs[0]->filter; }
/* }}} */

#ifndef free_mark
#define free_mark(mark) free((mark).path)
#endif

void nav_deinit(nav_t *nav)
{
	cvector_ffree(nav->selection, free);
	/* prev_selection _never_ holds allocated paths that are not already
	 * free'd in nav->selection */
	cvector_free(nav->prev_selection);
	cvector_ffree(nav->load, free);
	cvector_ffree(nav->marklist, free_mark);
	cache_deinit(&nav->dircache);
}

#undef free_mark
