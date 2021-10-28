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
#include "fm.h"
#include "notify.h"
#include "util.h"

#define DIRCACHE_SIZE 31

void fm_update_preview(fm_t *fm);

static dir_t *load_dir(fm_t *fm, const char *path);
static void update_watchers(fm_t *fm);
static void remove_preview(fm_t *fm);
static void mark_save(fm_t *fm, char mark, const char *path);

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

static void populate(fm_t *fm)
{
	int i;

	const char *s;
	char pwd[PATH_MAX];
	if ((s = getenv("PWD"))) {
		strncpy(pwd, s, sizeof(pwd)-1);
	} else {
		getcwd(pwd, sizeof(pwd));
	}

	fm->dirs[0] = load_dir(fm, pwd); /* current dir */
	dir_t *d = fm->dirs[0];
	for (i = 1; i < fm->ndirs; i++) {
		if ((s = dir_parent(d))) {
			d = load_dir(fm, s);
			fm->dirs[i] = d;
			dir_sel(d, fm->dirs[i-1]->name);
		} else {
			fm->dirs[i] = NULL;
		}
	}
}

void fm_init(fm_t *fm)
{
	if (cfg.startpath) {
		if ((chdir(cfg.startpath)) != 0) {
			error("chdir: %s", strerror(errno));
		} else {
			setenv("PWD", cfg.startpath, true);
		}
	}

	cache_init(&fm->dircache, DIRCACHE_SIZE, (void (*)(void*)) dir_free);

	fm->ndirs = 0;
	fm->dirs = NULL;
	fm->height = 0;
	fm->load = NULL;
	fm->marklist = NULL;
	fm->mode = MODE_COPY;
	fm->prev_selection = NULL;
	fm->selection_len = 0;
	fm->selection = NULL;
	fm->visual = false;
	fm->preview = NULL;

	fm->ndirs = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
	cvector_grow(fm->dirs, fm->ndirs);

	populate(fm);

	update_watchers(fm);

	if (cfg.startfile) {
		fm_sel(fm, cfg.startfile);
	}

	fm_update_preview(fm);
}

void fm_recol(fm_t *fm)
{
	int i;
	/* We silently disable previews without changing cfg.preview.
	 * */
	const int l = max(1, cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0));

	remove_preview(fm);
	for (i = 0; i < fm->ndirs; i++) {
		cache_insert(&fm->dircache, fm->dirs[i], fm->dirs[i]->path);
	}

	cvector_set_size(fm->dirs, l);
	fm->ndirs = l;

	populate(fm);

	update_watchers(fm);

	fm_update_preview(fm);
}

bool fm_chdir(fm_t *fm, const char *path, bool save)
{
	log_trace("fm_chdir: %s", path);

	fm_selection_visual_stop(fm);

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

	if (save && !fm->dirs[0]->error) {
		mark_save(fm, '\'', fm->dirs[0]->path);
	}

	remove_preview(fm);
	int i;
	for (i = 0; i < fm->ndirs; i++) {
		if (fm->dirs[i]) {
			cache_insert(&fm->dircache, fm->dirs[i], fm->dirs[i]->path);
		}
	}
	populate(fm);
	update_watchers(fm);
	fm_update_preview(fm);

	return true;
}

static void update_watchers(fm_t *fm)
{
	const int l = fm->ndirs;
	const char *w[l];

	int i;
	for (i = 0; i < l; i++) {
		w[i] = fm->dirs[i] != NULL ? fm->dirs[i]->path : NULL;
	}

	notify_set_watchers(w, l);
}

void fm_sort(fm_t *fm)
{
	int i;
	for (i = 0; i < fm->ndirs; i++) {
		if (fm->dirs[i]) {
			fm->dirs[i]->hidden = cfg.hidden;
			/* TODO: maybe we can select the closest non-hidden file in case the
			 * current one will be hidden (on 2021-10-17) */
			if (fm->dirs[i]->len > 0) {
				const char *name = dir_current_file(fm->dirs[i])->name;
				dir_sort(fm->dirs[i]);
				dir_sel(fm->dirs[i], name);
			}
		}
	}
	if (fm->preview) {
		fm->preview->hidden = cfg.hidden;
		if (fm->preview->len > 0) {
			const char *name = dir_current_file(fm->preview)->name;
			dir_sort(fm->preview);
			dir_sel(fm->preview, name);
		}
	}
}

void fm_hidden_set(fm_t *fm, bool hidden)
{
	cfg.hidden = hidden;
	fm_sort(fm);
	fm_update_preview(fm);
}

/* TODO: It actually makes more sense to update dir access times when leaving
 * the directory, but we would have to find the directory in the heap first.
 * For now, we update when inserting/updating the dir.
 * (on 2021-08-03) */
static dir_t *load_dir(fm_t *fm, const char *path)
{
	dir_t *dir;
	char fullpath[PATH_MAX];
	if (path[0] != '/') {
		realpath(path, fullpath);
		path = fullpath;
	}

	if ((dir = cache_take(&fm->dircache, path))) {
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

file_t *fm_current_file(const fm_t *fm)
{
	return dir_current_file(fm->dirs[0]);
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
bool fm_insert_dir(fm_t *fm, dir_t *dir)
{
	dir_t *olddir;
	int i;

	if ((olddir = cache_take(&fm->dircache, dir->path))) {
		/* replace in dir cache */
		copy_attrs(dir, olddir);
		dir_free(olddir);
		cache_insert(&fm->dircache, dir, dir->path);
	} else {
		/* check if it an active directory */
		if (fm->preview && streq(fm->preview->path, dir->path)) {
			copy_attrs(dir, fm->preview);
			dir_free(fm->preview);
			fm->preview = dir;
			return true;
		} else {
			for (i = 0; i < fm->ndirs; i++) {
				if (fm->dirs[i] && streq(fm->dirs[i]->path, dir->path)) {
					copy_attrs(dir, fm->dirs[i]);
					dir_free(fm->dirs[i]);
					fm->dirs[i] = dir;
					if (i == 0) {
						/* current dir */
						fm_update_preview(fm);
					}
					return true;
				}
			}
		}
		dir_free(dir);
	}
	return false;
}

void fm_check_dirs(const fm_t *fm)
{
	int i;
	for (i = 0; i < fm->ndirs; i++) {
		if (!fm->dirs[i]) {
			continue;
		}
		if (!dir_check(fm->dirs[i])) {
			async_dir_load(fm->dirs[i]->path);
		}
	}
	if (fm->preview && !dir_check(fm->preview)) {
		async_dir_load(fm->preview->path);
	}
}

void fm_drop_cache(fm_t *fm)
{
	int i;

	for (i = 0; i < fm->ndirs; i++) {
		if (fm->dirs[i]) {
			dir_free(fm->dirs[i]);
		}
	}
	remove_preview(fm);

	cache_clear(&fm->dircache);

	populate(fm);
	fm_update_preview(fm);
	update_watchers(fm);
}

static void remove_preview(fm_t *fm)
{
	if (fm->preview) {
		notify_remove_watcher(fm->preview->path);
		cache_insert(&fm->dircache, fm->preview, fm->preview->path);
		fm->preview = NULL;
	}
}

void fm_update_preview(fm_t *fm)
{
	int i;
	if (!cfg.preview) {
		remove_preview(fm);
		return;
	}

	const file_t *file = fm_current_file(fm);
	if (file && file_isdir(file)) {
		if (fm->preview) {
			if (streq(fm->preview->path, file->path)) {
				return;
			}
			for (i = 0; i < fm->ndirs; i++) {
				if (fm->dirs[i] && streq(fm->preview->path, fm->dirs[i]->path)) {
					break;
				}
			}
			if (i == fm->ndirs) {
				notify_remove_watcher(fm->preview->path);
				cache_insert(&fm->dircache, fm->preview, fm->preview->path);
			}
		}
		fm->preview = load_dir(fm, file->path);
		notify_add_watcher(fm->preview->path);
	} else {
		// file preview or empty
		if (fm->preview) {
			for (i = 0; i < fm->ndirs; i++) {
				if (fm->dirs[i] && streq(fm->preview->path, fm->dirs[i]->path)) {
					break;
				}
			}
			if (i == fm->ndirs) {
				notify_remove_watcher(fm->preview->path);
				cache_insert(&fm->dircache, fm->preview, fm->preview->path);
			}
			fm->preview = NULL;
		}
	}
}

/* selection {{{ */
void fm_selection_clear(fm_t *fm)
{
	cvector_fclear(fm->selection, free);
	fm->selection_len = 0;
}

void fm_selection_add_file(fm_t *fm, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(fm->selection); i++) {
		if (!fm->selection[i]) {
			continue;
		}
		if (streq(fm->selection[i], path)) {
			return;
		}
	}
	cvector_push_back(fm->selection, strdup(path));
	fm->selection_len++;
}

void fm_selection_set(fm_t *fm, cvector_vector_type(char*) selection)
{
	fm_selection_clear(fm);
	cvector_free(fm->selection);
	fm->selection = selection;
	fm->selection_len = cvector_size(selection); // assume selection isnt sparse
}

void selection_toggle_file(fm_t *fm, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(fm->selection); i++) {
		if (!fm->selection[i]) {
			continue;
		}
		if (streq(fm->selection[i], path)) {
			free(fm->selection[i]);
			fm->selection[i] = NULL;
			fm->selection_len--;
			if (fm->selection_len == 0) {
				cvector_set_size(fm->selection, 0);
			}
			return;
		}
	}
	cvector_push_back(fm->selection, strdup(path));
	fm->selection_len++;
}

void fm_selection_toggle_current(fm_t *fm)
{
	file_t *file;
	if (!fm->visual) {
		if ((file = fm_current_file(fm))) {
			selection_toggle_file(fm, file->path);
		}
	}
}

void fm_selection_reverse(fm_t *fm)
{
	const dir_t *dir = fm->dirs[0];
	for (int i = 0; i < dir->len; i++) {
		selection_toggle_file(fm, dir->files[i]->path);
	}
}

void fm_selection_visual_start(fm_t *fm)
{
	size_t i;
	dir_t *dir;
	if (fm->visual) {
		return;
	}
	if (!(dir = fm->dirs[0])) {
		return;
	}
	if (dir->len == 0) {
		return;
	}
	fm->visual = true;
	fm->visual_anchor = dir->ind;
	fm_selection_add_file(fm, dir->files[dir->ind]->path);
	for (i = 0; i < cvector_size(fm->selection); i++) {
		if (!fm->selection[i]) {
			continue;
		}
		cvector_push_back(fm->prev_selection, fm->selection[i]);
	}
}

void fm_selection_visual_stop(fm_t *fm)
{
	if (!fm->visual) {
		return;
	}
	fm->visual = false;
	fm->visual_anchor = 0;
	/* we dont free anything here because the old selection is always a subset of the
	 * new slection */
	cvector_set_size(fm->prev_selection, 0);
}

void fm_selection_visual_toggle(fm_t *fm)
{
	if (fm->visual) {
		fm_selection_visual_stop(fm);
	} else {
		fm_selection_visual_start(fm);
	}
}

void selection_visual_update(fm_t *fm, int origin, int from, int to)
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
	const dir_t *dir = fm->dirs[0];
	for (; lo <= hi; lo++) {
		/* never unselect the old selection */
		if (!cvector_contains(dir->files[lo]->path, fm->prev_selection)) {
			selection_toggle_file(fm, dir->files[lo]->path);
		}
	}
}

void fm_selection_write(const fm_t *fm, const char *path)
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

	if (fm->selection_len > 0) {
		size_t i;
		for (i = 0; i< cvector_size(fm->selection); i++) {
			if (!fm->selection[i]) {
				continue;
			}
			fputs(fm->selection[i], fp);
			fputc('\n', fp);
		}
	} else {
		if ((f = fm_current_file(fm))) {
			fputs(f->path, fp);
			fputc('\n', fp);
		}
	}
	fclose(fp);
}

/* }}} */

/* fmigation {{{ */
static bool fm_move(fm_t *fm, int ct)
{
	dir_t *dir = fm->dirs[0];
	const int cur = dir->ind;

	dir->ind = max(min(dir->ind + ct, dir->len - 1), 0);
	if (ct < 0) {
		dir->pos = min(max(cfg.scrolloff, dir->pos + ct), dir->ind);
	} else {
		dir->pos = max(min(fm->height - 1 - cfg.scrolloff, dir->pos + ct), fm->height - dir->len + dir->ind);
	}

	if (dir->ind != cur) {
		if (fm->visual) {
			selection_visual_update(fm, fm->visual_anchor, cur,
					dir->ind);
		}

		fm_update_preview(fm);
		return true;
	}
	/* We actually have to redraw becuase we could be selecting the last
	 * file in the directory but not actually moving. */
	return dir->ind == dir->len - 1;
}

bool fm_up(fm_t *fm, int ct) { return fm_move(fm, -ct); }

bool fm_down(fm_t *fm, int ct) { return fm_move(fm, ct); }

bool fm_top(fm_t *fm) { return fm_up(fm, fm->dirs[0]->ind); }

bool fm_bot(fm_t *fm)
{
	return fm_down(fm, fm->dirs[0]->len - fm->dirs[0]->ind);
}

void fm_sel(fm_t *fm, const char *name)
{
	dir_sel(fm->dirs[0], name);
	fm_update_preview(fm);
}

file_t *fm_open(fm_t *fm)
{
	file_t *file;

	if (!(file = fm_current_file(fm))) {
		return NULL;
	}
	fm_selection_visual_stop(fm); /* before or after chdir? */
	if (!file_isdir(file)) {
		return file;
	}
	fm_chdir(fm, concatpath(fm->dirs[0]->path, file->name), false);
	return NULL;
}

void fm_updir(fm_t *fm)
{
	if (dir_isroot(fm->dirs[0])) {
		return;
	}
	const char *name = fm->dirs[0]->name;
	fm_chdir(fm, dir_parent(fm->dirs[0]), false);
	fm_sel(fm, name);
	fm_update_preview(fm);
}

/* }}} */

/* marks {{{ */
static void mark_save(fm_t *fm, char mark, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(fm->marklist); i++) {
		if (fm->marklist[i].mark == mark) {
			if (!streq(fm->marklist[i].path, path)) {
				fm->marklist[i].path =
					realloc(fm->marklist[i].path,
							sizeof(char) * (strlen(path) + 1));
				strcpy(fm->marklist[i].path, path);
			}
			return;
		}
	}
	mark_t newmark = {.mark = mark, .path = strdup(path)};
	cvector_push_back(fm->marklist, newmark);
}

bool fm_mark_load(fm_t *fm, char mark)
{
	size_t i;
	for (i = 0; i < cvector_size(fm->marklist); i++) {
		if (fm->marklist[i].mark == mark) {
			if (streq(fm->marklist[i].path, fm->dirs[0]->path)) {
				log_info("mark is current dir: %c", mark);
			} else {
				fm_chdir(fm, fm->marklist[i].path, true);
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
void fm_load_files(fm_t *fm, enum movemode_e mode)
{
	fm_selection_visual_stop(fm);
	fm->mode = mode;
	if (fm->selection_len == 0) {
		fm_selection_toggle_current(fm);
	}
	fm_load_clear(fm);
	char **tmp = fm->load;
	fm->load = fm->selection;
	cvector_unsparse(fm->load);
	fm->selection = tmp;
	fm->selection_len = 0;
}

void fm_load_clear(fm_t *fm)
{
	cvector_fclear(fm->load, free);
}

char * const *fm_get_load(const fm_t *fm) { return fm->load; }

enum movemode_e fm_get_mode(const fm_t *fm) { return fm->mode; }

void fm_cut(fm_t *fm) { fm_load_files(fm, MODE_MOVE); }

void fm_copy(fm_t *fm) { fm_load_files(fm, MODE_COPY); }
/* }}} */

/* filter {{{ */
void fm_filter(fm_t *fm, const char *filter)
{
	dir_t *d;
	file_t *f;

	d = fm->dirs[0];
	f = dir_current_file(d);
	dir_filter(d, filter);
	dir_sel(d, f ? f->name : NULL);
	fm_update_preview(fm);
}

const char *fm_filter_get(const fm_t *fm) { return fm->dirs[0]->filter; }
/* }}} */

#ifndef free_mark
#define free_mark(mark) free((mark).path)
#endif

void fm_deinit(fm_t *fm)
{
	cvector_ffree(fm->selection, free);
	/* prev_selection _never_ holds allocated paths that are not already
	 * free'd in fm->selection */
	cvector_free(fm->prev_selection);
	cvector_ffree(fm->load, free);
	cvector_ffree(fm->marklist, free_mark);
	cache_deinit(&fm->dircache);
}

#undef free_mark
