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

typedef struct nav_t {
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

	/* Height of the nav in the ui, needed to adjust pos for each dir */
	int height;

	/* Visual selection mode active */
	bool visual;
	/* Index of the beginning of the visual selection */
	int visual_anchor;

} nav_t;

/*
 * Moves to the correct starting directory, loads initial dirs and sets up
 * previews and inotify watchers.
 */
void nav_init(nav_t *nav);

/*
 * Unloads directories and frees all resources.
 */
void nav_destroy(nav_t *nav);

/*
 * Updates the number of loaded directories, called after the number of columns
 * changes.
 */
void nav_recol(nav_t *nav);

/*
 * Move cursor CT up in the current directory.
 */
bool nav_up(nav_t *nav, int ct);

/*
 * Move cursor CT down in the current directory.
 */
bool nav_down(nav_t *nav, int ct);

/*
 * Move cursor to the top of the current directory.
 */
bool nav_top(nav_t *nav);

/*
 * Move cursor to the bottom of the current directory.
 */
bool nav_bot(nav_t *nav);

/*
 * Changes directory to the directory given by PATH. If SAVE then the current
 * directory will be saved as the special "'" mark.
 */
bool nav_chdir(nav_t *nav, const char *path, bool save);

/*
 * Open the currently selected file: if it is a directory, chdir into it.
 * Otherwise return the file so that the caller can open it.
 */
file_t *nav_open(nav_t *nav);

/*
 * Chdir to the parent of the current directory.
 */
void nav_updir(nav_t *nav);

/*
 * Move the cursor the file with FILENAME if it exists. Otherwise leaves the
 * cursor at the closest valid position (i.e. after the number of files
 * decreases)
 */
void nav_sel(nav_t *nav, const char *filename);

/*
 * Apply the filter string given by FILTER to the current directory.
 */
void nav_filter(nav_t *nav, const char *filter);

/*
 * Return the filter string of the currently selected directory.
 */
const char *nav_filter_get(const nav_t *nav);

/*
 *  TODO: wrong way around? (on 2021-08-04)
 */
void nav_hidden_set(nav_t *nav, bool hidden);

/*
 * Checks all visible directories for changes on disk and schedules reloads if
 * necessary.
 */
void nav_check_dirs(const nav_t *nav);

/*
 * Insert a new DIR into the directory list.
 */
bool nav_insert_dir(nav_t *nav, dir_t *dir);

/*
 * Chdir to the directory saved as MARK.
 */
bool nav_mark_load(nav_t *nav, char mark);

/*
 * Toggles the selection of the currently selected file.
 */
void nav_selection_toggle_current(nav_t *nav);

/*
 * Clear the selection completely.
 */
void nav_selection_clear(nav_t *nav);

/*
 * Reverse the file selection.
 */
void nav_selection_reverse(nav_t *nav);

/*
 * Begin visual selection mode.
 */
void nav_selection_visual_start(nav_t *nav);

/*
 * End visual selection mode.
 */
void nav_selection_visual_stop(nav_t *nav);

/*
 * Toggle visual selection mode.
 */
void nav_selection_visual_toggle(nav_t *nav);

/*
 * Write the current celection to the file given as PATH.
 * Directories are created as needed.
 */
void nav_selection_write(const nav_t *nav, const char *path);

/*
 * Move the current selection to the copy buffer.
 */
void nav_copy(nav_t *nav);

/*
 * Move the current selection to the move buffer.
 */
void nav_cut(nav_t *load);

/*
 * Clear copy/move buffer.
 */
void nav_load_clear(nav_t *nav);

/*
 * Get the list of files in copy/move buffer.
 */
char * const* nav_get_load(const nav_t *nav);

/*
 * Get the mode current load, one of MODE_COPY, MODE_MOVE.
 */
enum movemode_e nav_get_mode(const nav_t *nav);

/*
 * Current file of the current directory. Can be NULL.
 */
file_t *nav_current_file(const nav_t *nav);

/*
 * Current directory. Never NULL.
 */
dir_t *nav_current_dir(const nav_t *nav);

/*
 * Compare PATH against each path in SELECTION to check if it is contained.
 */
bool cvector_contains(const char *path, cvector_vector_type(char*) selection);

/*
 * Drop directory cache and reload visible directories from disk.
 */
void nav_drop_cache(nav_t *nav);

#define nav_current_dir(nav) (nav)->dirs[0]

#define nav_preview_dir(nav) (nav)->preview

void nav_selection_add_file(nav_t *nav, const char *path);

void nav_selection_set(nav_t *nav, cvector_vector_type(char*) selection);

#endif /* NAV_H */
