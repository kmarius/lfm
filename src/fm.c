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
#include "fm.h"
#include "log.h"
#include "lualfm.h"
#include "notify.h"
#include "util.h"

#define T Fm


#define FM_INITIALIZER ((T){ \
		.load.mode = MODE_COPY, \
		})

#define is_absolute(path) (*(path) == '/')
#define is_relative(path) !is_absolute(path)


static Dir *fm_load_dir(T *t, const char *path);
static void fm_update_watchers(T *t);
static void fm_remove_preview(T *t);
static void fm_mark_save(T *t, char mark, const char *path);
static void fm_populate(T *t);

bool fm_bot(T *t);
bool fm_top(T *t);
bool fm_down(T *t, int16_t ct);
bool fm_up(T *t, int16_t ct);
void fm_cursor_move_to_ind(T *t, uint16_t ind);


void fm_init(T *t)
{
	if (cfg.startpath) {
		if (chdir(cfg.startpath) != 0)
			error("chdir: %s", strerror(errno));
		else
			setenv("PWD", cfg.startpath, true);
	}

	*t = FM_INITIALIZER;

	t->dirs.length = cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0);
	cvector_grow(t->dirs.visible, t->dirs.length);

	cache_init(&t->dirs.cache, DIRCACHE_SIZE, (void (*)(void*)) dir_destroy);
	fm_populate(t);

	fm_update_watchers(t);

	if (cfg.startfile)
		fm_move_cursor_to(t, cfg.startfile);

	fm_update_preview(t);
}


void fm_deinit(T *t)
{
	for (uint8_t i = 0; i < t->dirs.length; i++)
		dir_destroy(t->dirs.visible[i]);
	cvector_ffree(t->selection.files, free);
	/* prev_selection _never_ holds allocated paths that are not already
	 * free'd in fm->selection */
	cvector_free(t->selection.previous);
	cvector_ffree(t->load.files, free);
	cache_deinit(&t->dirs.cache);

#define mark_free(mark) free((mark).path)
	cvector_ffree(t->marks, mark_free);
#undef mark_free
}


static void fm_populate(T *t)
{
	char pwd[PATH_MAX];

	const char *s = getenv("PWD");
	if (s)
		strncpy(pwd, s, sizeof(pwd)-1);
	else
		getcwd(pwd, sizeof(pwd));

	t->dirs.visible[0] = fm_load_dir(t, pwd); /* current dir */
	t->dirs.visible[0]->visible = true;
	Dir *d = fm_current_dir(t);
	for (uint16_t i = 1; i < t->dirs.length; i++) {
		if ((s = dir_parent_path(d))) {
			d = fm_load_dir(t, s);
			d->visible = true;
			t->dirs.visible[i] = d;
			dir_cursor_move_to(d, t->dirs.visible[i-1]->name, t->height, cfg.scrolloff);
		} else {
			t->dirs.visible[i] = NULL;
		}
	}
}


void fm_recol(T *t)
{
	fm_remove_preview(t);
	for (uint16_t i = 0; i < t->dirs.length; i++) {
		if (t->dirs.visible[i]) {
			t->dirs.visible[i]->visible = false;
			cache_insert(&t->dirs.cache, t->dirs.visible[i], t->dirs.visible[i]->path);
		}
	}

	const uint16_t l = max(1, cvector_size(cfg.ratios) - (cfg.preview ? 1 : 0));
	cvector_grow(t->dirs.visible, l);
	cvector_set_size(t->dirs.visible, l);
	t->dirs.length = l;

	fm_populate(t);

	fm_update_watchers(t);

	fm_update_preview(t);
}


bool fm_chdir(T *t, const char *path, bool save)
{
	fm_selection_visual_stop(t);

	char fullpath[PATH_MAX];
	if (is_relative(path)) {
		realpath(path, fullpath);
		path = fullpath;
	}

	uint64_t t0 = current_millis();
	if (chdir(path) != 0) {

		uint64_t t1 = current_millis();
		if (t1 - t0 > 10)
			log_debug("chdir(\"%s\") took %ums", path, t1-t0);

		error("chdir: %s", strerror(errno));
		return false;
	}

	uint64_t t1 = current_millis();
	if (t1 - t0 > 10)
		log_debug("chdir(\"%s\") took %ums", path, t1-t0);

	notify_set_watchers(NULL, 0);

	setenv("PWD", path, true);

	if (save && !fm_current_dir(t)->error)
		fm_mark_save(t, '\'', fm_current_dir(t)->path);

	fm_remove_preview(t);
	for (uint16_t i = 0; i < t->dirs.length; i++) {
		if (t->dirs.visible[i]) {
			t->dirs.visible[i]->visible = false;
			cache_insert(&t->dirs.cache, t->dirs.visible[i], t->dirs.visible[i]->path);
		}
	}
	fm_populate(t);
	fm_update_watchers(t);
	fm_update_preview(t);

	return true;
}


static inline void fm_update_watchers(T *t)
{
	// watcher for preview is updated in update_preview
	notify_set_watchers(t->dirs.visible, t->dirs.length);
}


/* TODO: maybe we can select the closest non-hidden file in case the
 * current one will be hidden (on 2021-10-17) */
static inline void fm_sort_and_reselect(T *t, Dir *dir)
{
	if (!dir)
		return;

	dir->hidden = cfg.hidden;
	const File *file = dir_current_file(dir);
	dir_sort(dir);
	if (file)
		dir_cursor_move_to(dir, file_name(file), t->height, cfg.scrolloff);
}


void fm_sort(T *t)
{
	for (uint16_t i = 0; i < t->dirs.length; i++)
		fm_sort_and_reselect(t, t->dirs.visible[i]);
	fm_sort_and_reselect(t, t->dirs.preview);
}


void fm_hidden_set(T *t, bool hidden)
{
	cfg.hidden = hidden;
	fm_sort(t);
	fm_update_preview(t);
}


static Dir *fm_load_dir(T *t, const char *path)
{
	char fullpath[PATH_MAX];
	if (is_relative(path)) {
		realpath(path, fullpath);
		path = fullpath;
	}

	Dir *dir = cache_take(&t->dirs.cache, path);
	if (dir) {
		async_dir_check(dir);
		dir->hidden = cfg.hidden;
		dir_sort(dir);
	} else {
		/* At this very point, we should not print this new directory, but
		 * start a timer for, say, 250ms. When the timer runs out we draw the
		 * "loading" directory regardless. The timer should be cancelled when:
		 * 1. the actual directory arrives after loading from disk
		 * 2. we navigate to a different directory (possibly restart a timer there)
		 *
		 * Check how this behaves in the preview pane when just scrolling over
		 * directories.
		 */
		dir = dir_new_loading(path);
		dir->hidden = cfg.hidden;
		async_dir_load(dir, false);
	}
	return dir;
}


void fm_check_dirs(const T *t)
{
	for (uint16_t i = 0; i < t->dirs.length; i++) {
		if (!t->dirs.visible[i])
			continue;

		if (!dir_check(t->dirs.visible[i]))
			async_dir_load(t->dirs.visible[i], true);
	}

	if (t->dirs.preview && !dir_check(t->dirs.preview))
		async_dir_load(t->dirs.preview, true);
}


void fm_drop_cache(T *t)
{
	/* TODO: disabled, force reload everything instead? (on 2022-01-25) */
	return;
	notify_set_watchers(NULL, 0);

	for (uint16_t i = 0; i < t->dirs.length; i++) {
		if (t->dirs.visible[i])
			dir_destroy(t->dirs.visible[i]);
	}
	fm_remove_preview(t);

	cache_clear(&t->dirs.cache);

	fm_populate(t);
	fm_update_preview(t);
	fm_update_watchers(t);
}


void fm_reload(T *t)
{
	for (uint16_t i = 0; i < t->dirs.length; i++) {
		if (t->dirs.visible[i]) {
			t->dirs.visible[i]->flatten_level = 0;
			async_dir_load(t->dirs.visible[i], true);
		}
	}
	if (t->dirs.preview) {

		t->dirs.preview->flatten_level = 0;
		async_dir_load(t->dirs.preview, true);
	}
}


static void fm_remove_preview(T *t)
{
	if (!t->dirs.preview)
		return;

	notify_remove_watcher(t->dirs.preview);
	t->dirs.preview->visible = false;
	cache_insert(&t->dirs.cache, t->dirs.preview, t->dirs.preview->path);
	t->dirs.preview = NULL;
}


void fm_update_preview(T *t)
{
	if (!cfg.preview) {
		fm_remove_preview(t);
		return;
	}

	const File *file = fm_current_file(t);
	if (file && file_isdir(file)) {
		if (t->dirs.preview) {
			if (streq(t->dirs.preview->path, file_path(file)))
				return;

			/* don't remove watcher if it is a currently visible (non-preview) dir */
			uint16_t i;
			for (i = 0; i < t->dirs.length; i++) {
				if (t->dirs.visible[i] && streq(t->dirs.preview->path, t->dirs.visible[i]->path))
					break;
			}
			if (i >= t->dirs.length) {
				notify_remove_watcher(t->dirs.preview);
				t->dirs.preview->visible = false;
				cache_insert(&t->dirs.cache, t->dirs.preview, t->dirs.preview->path);
			}
		}
		t->dirs.preview = fm_load_dir(t, file_path(file));
		t->dirs.preview->visible = true;
		// sometimes very slow on smb (> 200ms)
		notify_add_watcher(t->dirs.preview);
	} else {
		// file preview or empty
		if (t->dirs.preview) {
			uint16_t i;
			for (i = 0; i < t->dirs.length; i++) {
				if (t->dirs.visible[i] && streq(t->dirs.preview->path, t->dirs.visible[i]->path))
					break;
			}
			if (i == t->dirs.length) {
				notify_remove_watcher(t->dirs.preview);
				t->dirs.preview->visible = false;
				cache_insert(&t->dirs.cache, t->dirs.preview, t->dirs.preview->path);
			}
			t->dirs.preview = NULL;
		}
	}
}


/* selection {{{ */

void fm_selection_clear(T *t)
{
	cvector_fclear(t->selection.files, free);
	t->selection.length = 0;
}


void fm_selection_add_file(T *t, const char *path)
{
	size_t i;
	for (i = 0; i < cvector_size(t->selection.files); i++) {
		if (t->selection.files[i] && streq(t->selection.files[i], path))
			return;
	}
	cvector_push_back(t->selection.files, strdup(path));
	t->selection.length++;
}


void fm_selection_set(T *t, cvector_vector_type(char*) selection)
{
	fm_selection_clear(t);
	cvector_free(t->selection.files);
	t->selection.files = selection;
	t->selection.length = cvector_size(selection); // assume selection isnt sparse
}


void selection_toggle_file(T *t, const char *path)
{
	for (size_t i = 0; i < cvector_size(t->selection.files); i++) {
		if (t->selection.files[i] && streq(t->selection.files[i], path)) {
			free(t->selection.files[i]);
			t->selection.files[i] = NULL;
			t->selection.length--;
			if (t->selection.length == 0)
				cvector_set_size(t->selection.files, 0);
			return;
		}
	}
	cvector_push_back(t->selection.files, strdup(path));
	t->selection.length++;
}


void fm_selection_toggle_current(T *t)
{
	if (t->visual.active) {
		return;
	}
	File *file = fm_current_file(t);
	if (file)
		selection_toggle_file(t, file_path(file));
}


void fm_selection_reverse(T *t)
{
	const Dir *dir = fm_current_dir(t);
	for (uint16_t i = 0; i < dir->length; i++)
		selection_toggle_file(t, file_path(dir->files[i]));
}


void fm_selection_visual_start(T *t)
{
	if (t->visual.active)
		return;

	Dir *dir = fm_current_dir(t);
	if (dir->length == 0)
		return;

	/* TODO: what actually happens if we change sortoptions while visual is
	 * active? (on 2021-11-15) */
	t->visual.active = true;
	t->visual.anchor = dir->ind;
	fm_selection_add_file(t, file_path(dir->files[dir->ind]));
	for (size_t i = 0; i < cvector_size(t->selection.files); i++) {
		if (t->selection.files[i])
			cvector_push_back(t->selection.previous, t->selection.files[i]);
	}
}


void fm_selection_visual_stop(T *t)
{
	if (!t->visual.active)
		return;

	t->visual.active = false;
	t->visual.anchor = 0;
	/* we dont free anything here because the old selection is always a subset of the
	 * new slection */
	cvector_set_size(t->selection.previous, 0);
}


void fm_selection_visual_toggle(T *t)
{
	if (t->visual.active)
		fm_selection_visual_stop(t);
	else
		fm_selection_visual_start(t);
}


static void selection_visual_update(T *t, uint16_t origin, uint16_t from, uint16_t to)
{
	/* TODO: this should be easier (on 2021-07-25) */
	uint16_t hi, lo;
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
	const Dir *dir = fm_current_dir(t);
	for (; lo <= hi; lo++) {
		/* never unselect the old selection */
		if (!cvector_contains_str(t->selection.previous, file_path(dir->files[lo])))
			selection_toggle_file(t, file_path(dir->files[lo]));
	}
}


void fm_selection_write(const T *t, const char *path)
{

	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	FILE *fp = fopen(path, "w");
	if (!fp) {
		error("selfile: %s", strerror(errno));
		return;
	}

	if (t->selection.length > 0) {
		for (size_t i = 0; i< cvector_size(t->selection.files); i++) {
			if (t->selection.files[i]) {
				fputs(t->selection.files[i], fp);
				fputc('\n', fp);
			}
		}
	} else {
		const File *file = fm_current_file(t);
		if (file) {
			fputs(file_path(file), fp);
			fputc('\n', fp);
		}
	}
	fclose(fp);
}

/* }}} */

/* navigation {{{ */

bool fm_cursor_move(T *t, int16_t ct)
{
	Dir *dir = fm_current_dir(t);
	const uint16_t cur = dir->ind;
	dir_cursor_move(dir, ct, t->height, cfg.scrolloff);
	if (dir->ind != cur) {
		if (t->visual.active)
			selection_visual_update(t, t->visual.anchor, cur, dir->ind);
		fm_update_preview(t);
	}
	return dir->ind != cur;
}


void fm_move_cursor_to(T *t, const char *name)
{
	dir_cursor_move_to(fm_current_dir(t), name, t->height, cfg.scrolloff);
	fm_update_preview(t);
}


File *fm_open(T *t)
{
	File *file = fm_current_file(t);
	if (!file)
		return NULL;

	fm_selection_visual_stop(t); /* before or after chdir? */
	if (!file_isdir(file))
		return file;

	fm_chdir(t, file_path(file), false);
	return NULL;
}


/* TODO: allow updir into directories that don't exist so we can move out of
 * deleted directories (on 2021-11-18) */
void fm_updir(T *t)
{
	if (dir_isroot(fm_current_dir(t)))
		return;

	const char *name = fm_current_dir(t)->name;
	fm_chdir(t, dir_parent_path(fm_current_dir(t)), false);
	fm_move_cursor_to(t, name);
	fm_update_preview(t);
}

/* }}} */

/* marks {{{ */
static void fm_mark_save(T *t, char mark, const char *path)
{
	for (size_t i = 0; i < cvector_size(t->marks); i++) {
		if (t->marks[i].mark == mark) {
			if (!streq(t->marks[i].path, path)) {
				t->marks[i].path = realloc(t->marks[i].path, sizeof(char) * (strlen(path) + 1));
				strcpy(t->marks[i].path, path);
			}
			return;
		}
	}
	cvector_push_back(t->marks, ((struct jump_mark) {mark, strdup(path),}));
}


bool fm_mark_load(T *t, char mark)
{
	for (size_t i = 0; i < cvector_size(t->marks); i++) {
		if (t->marks[i].mark == mark) {
			if (!streq(t->marks[i].path, fm_current_dir(t)->path))
				fm_chdir(t, t->marks[i].path, true);

			/* TODO: shouldn't return true if chdir fails (on 2021-07-22) */
			return true;
		}
	}
	error("no such mark: %c", mark);
	return false;
}
/* }}} */

/* load/copy/move {{{ */

/* TODO: Make it possible to append to cut/copy buffer (on 2021-07-25) */
void fm_load_files(T *t, enum movemode_e mode)
{
	fm_selection_visual_stop(t);
	t->load.mode = mode;
	if (t->selection.length == 0)
		fm_selection_toggle_current(t);
	fm_load_clear(t);
	char **tmp = t->load.files;
	t->load.files = t->selection.files;
	cvector_compact(t->load.files);
	t->selection.files = tmp;
	t->selection.length = 0;
}


void fm_load_clear(T *t)
{
	cvector_fclear(t->load.files, free);
}


char * const *fm_get_load(const T *t)
{
	return t->load.files;
}


enum movemode_e fm_get_mode(const T *t)
{
	return t->load.mode;
}


void fm_cut(T *t)
{
	fm_load_files(t, MODE_MOVE);
}


void fm_copy(T *t)
{
	fm_load_files(t, MODE_COPY);
}

/* }}} */

/* filter {{{ */

void fm_filter(T *t, const char *filter)
{
	Dir *dir = fm_current_dir(t);
	File *file = dir_current_file(dir);
	dir_filter(dir, filter);
	dir_cursor_move_to(dir, file ? file_name(file) : NULL, t->height, cfg.scrolloff);
	fm_update_preview(t);
}


const char *fm_filter_get(const T *t)
{
	return fm_current_dir(t)->filter;
}


/* }}} */

/* TODO: To reload flattened directories, more notify watchers are needed (on 2022-02-06) */
void fm_flatten(T *t, uint8_t level)
{
	fm_current_dir(t)->flatten_level = level;
	async_dir_load(fm_current_dir(t), true);
}

#undef T
