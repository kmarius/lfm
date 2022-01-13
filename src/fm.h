#pragma once

#include <stdbool.h>

#include "cache.h"
#include "cvector.h"
#include "dir.h"

#define DIRCACHE_SIZE 63

enum movemode_e {
	MODE_MOVE,
	MODE_COPY,
};

typedef struct mark_t {
	char mark;
	char *path;
} mark_t;

typedef struct fm_t {

	/* Height of the fm in the ui, needed to adjust pos for each dir */
	int height;

	struct {
		/* Visible directories excluding preview, vector of dir_t* */
		cvector_vector_type(dir_t *) visible;
		int len;

		cache_t cache;

		/* preview directory, NULL if there is none, e.g. if the cursor is resting on a file */
		dir_t *preview;
	} dirs;

	/* List of quickmarks including "'" */
	cvector_vector_type(mark_t) marks;

	struct {
		/* Current selection, as a vector of paths */
		cvector_vector_type(char *) files;
		int len;

		/* Previous seletction, needed for visual selection mode */
		cvector_vector_type(char *) previous;
	} selection;

	struct {
		/* Copy/move buffer, vector of paths */
		cvector_vector_type(char *) files;
		enum movemode_e mode;
	} load;

	struct {
		bool active : 1;
		int anchor;
	} visual;
} fm_t;

/*
 * Moves to the correct starting directory, loads initial dirs and sets up
 * previews and inotify watchers.
 */
void fm_init(fm_t *fm);

/*
 * Unloads directories and frees all resources.
 */
void fm_deinit(fm_t *fm);

/*
 * Updates the number of loaded directories, called after the number of columns
 * changes.
 */
void fm_recol(fm_t *fm);

/*
 * Move cursor `ct` up in the current directory.
 */
bool fm_up(fm_t *fm, int ct);

/*
 * Move cursor `ct` down in the current directory.
 */
bool fm_down(fm_t *fm, int ct);

/*
 * Move cursor to the top of the current directory.
 */
bool fm_top(fm_t *fm);

/*
 * Move cursor to the bottom of the current directory.
 */
bool fm_bot(fm_t *fm);

/*
 * Changes directory to the directory given by `path`. If `save` then the current
 * directory will be saved as the special "'" mark.
 */
bool fm_chdir(fm_t *fm, const char *path, bool save);

/*
 * Open the currently selected file: if it is a directory, chdir into it.
 * Otherwise return the file so that the caller can open it.
 */
file_t *fm_open(fm_t *fm);

/*
 * Chdir to the parent of the current directory.
 */
void fm_updir(fm_t *fm);

/*
 * Move the cursor the file with `name` if it exists. Otherwise leaves the
 * cursor at the closest valid position (i.e. after the number of files
 * decreases)
 */
void fm_move_to(fm_t *fm, const char *name);

/*
 * Move the cursor the file at index `ind`.
 */
void fm_move_to_ind(fm_t *fm, int ind);

/*
 * Apply the filter string given by `filter` to the current directory.
 */
void fm_filter(fm_t *fm, const char *filter);

/*
 * Return the filter string of the currently selected directory.
 */
const char *fm_filter_get(const fm_t *fm);

/*
 *  TODO: wrong way around? (on 2021-08-04)
 */
void fm_hidden_set(fm_t *fm, bool hidden);

/*
 * Checks all visible directories for changes on disk and schedules reloads if
 * necessary.
 */
void fm_check_dirs(const fm_t *fm);

/*
 * Chdir to the directory saved as `mark`.
 */
bool fm_mark_load(fm_t *fm, char mark);

/*
 * Toggles the selection of the currently selected file.
 */
void fm_selection_toggle_current(fm_t *fm);

/*
 * Add `path` to the current selection if not already contained.
 */
void fm_selection_add_file(fm_t *fm, const char *path);

/*
 * Replace the current selection.
 */
void fm_selection_set(fm_t *fm, cvector_vector_type(char*) selection);

/*
 * Clear the selection completely.
 */
void fm_selection_clear(fm_t *fm);

/*
 * Reverse the file selection.
 */
void fm_selection_reverse(fm_t *fm);

/*
 * Begin visual selection mode.
 */
void fm_selection_visual_start(fm_t *fm);

/*
 * End visual selection mode.
 */
void fm_selection_visual_stop(fm_t *fm);

/*
 * Toggle visual selection mode.
 */
void fm_selection_visual_toggle(fm_t *fm);

/*
 * Write the current celection to the file given as `path`.
 * Directories are created as needed.
 */
void fm_selection_write(const fm_t *fm, const char *path);

/*
 * Move the current selection to the copy buffer.
 */
void fm_copy(fm_t *fm);

/*
 * Move the current selection to the move buffer.
 */
void fm_cut(fm_t *fm);

/*
 * Clear copy/move buffer.
 */
void fm_load_clear(fm_t *fm);

/*
 * Get the list of files in copy/move buffer. Returns a cvector of char*.
 */
char * const* fm_get_load(const fm_t *fm);

/*
 * Get the mode current load, one of `MODE_COPY`, `MODE_MOVE`.
 */
enum movemode_e fm_get_mode(const fm_t *fm);

/*
 * Current directory. Never `NULL`.
 */
#define fm_current_dir(fm) (fm)->dirs.visible[0]

/*
 * Current file of the current directory. Can be `NULL`.
 */
#define fm_current_file(fm) dir_current_file(fm_current_dir(fm))

#define fm_preview_dir(fm) (fm)->preview

/*
 * Compare PATH against each path in SELECTION to check if it is contained.
 */
bool cvector_contains(const char *path, cvector_vector_type(char*) selection);

/*
 * Drop directory cache and reload visible directories from disk.
 */
void fm_drop_cache(fm_t *fm);

/*
 * Apply an update to a directory. Returns `true` if the directory is visible
 * and a redraw is necessary.
 */
bool fm_update_dir(fm_t *fm, dir_t *dir, dir_t *update);
