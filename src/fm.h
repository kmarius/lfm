#ifndef NAV_H
#define NAV_H

#include <dirent.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cvector.h"
#include "dir.h"
#include "cache.h"

enum movemode_e {
	MODE_MOVE,
	MODE_COPY,
};

typedef struct mark_t {
	char mark;
	char *path;
} mark_t;

typedef struct fm_t {
	/* All loaded directories, not only visible ones */
	cache_t dircache;

	/* Visible directories excluding preview, vector of dir_t* */
	cvector_vector_type(dir_t *) dirs;
	int ndirs;

	/* preview directory, NULL if there is none, e.g. if a file is selected */
	dir_t *preview;

	/* List of quickmarks including "'" */
	cvector_vector_type(mark_t) marklist;

	/* Current selection, as a vector of paths */
	cvector_vector_type(char *) selection;
	int selection_len;

	/* Previous seletction, needed for visual selection mode */
	cvector_vector_type(char *) prev_selection;

	/* Copy/move buffer, vector of paths */
	cvector_vector_type(char *) load;
	enum movemode_e mode;

	/* Height of the fm in the ui, needed to adjust pos for each dir */
	int height;

	/* Visual selection mode active */
	bool visual;
	/* Index of the beginning of the visual selection */
	int visual_anchor;

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
 * Move cursor CT up in the current directory.
 */
bool fm_up(fm_t *fm, int ct);

/*
 * Move cursor CT down in the current directory.
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
 * Changes directory to the directory given by PATH. If SAVE then the current
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
 * Move the cursor the file with FILENAME if it exists. Otherwise leaves the
 * cursor at the closest valid position (i.e. after the number of files
 * decreases)
 */
void fm_sel(fm_t *fm, const char *filename);

/*
 * Apply the filter string given by FILTER to the current directory.
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
 * Insert a new DIR into the directory list.
 */
bool fm_insert_dir(fm_t *fm, dir_t *dir);

/*
 * Chdir to the directory saved as MARK.
 */
bool fm_mark_load(fm_t *fm, char mark);

/*
 * Toggles the selection of the currently selected file.
 */
void fm_selection_toggle_current(fm_t *fm);

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
 * Write the current celection to the file given as PATH.
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
void fm_cut(fm_t *load);

/*
 * Clear copy/move buffer.
 */
void fm_load_clear(fm_t *fm);

/*
 * Get the list of files in copy/move buffer.
 */
char * const* fm_get_load(const fm_t *fm);

/*
 * Get the mode current load, one of MODE_COPY, MODE_MOVE.
 */
enum movemode_e fm_get_mode(const fm_t *fm);

/*
 * Current file of the current directory. Can be NULL.
 */
file_t *fm_current_file(const fm_t *fm);

/*
 * Current directory. Never NULL.
 */
dir_t *fm_current_dir(const fm_t *fm);

/*
 * Compare PATH against each path in SELECTION to check if it is contained.
 */
bool cvector_contains(const char *path, cvector_vector_type(char*) selection);

/*
 * Drop directory cache and reload visible directories from disk.
 */
void fm_drop_cache(fm_t *fm);

#define fm_current_dir(fm) (fm)->dirs[0]

#define fm_preview_dir(fm) (fm)->preview

void fm_selection_add_file(fm_t *fm, const char *path);

void fm_selection_set(fm_t *fm, cvector_vector_type(char*) selection);

#endif /* NAV_H */