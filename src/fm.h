#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cache.h"
#include "cvector.h"
#include "dir.h"
#include "file.h"

#define DIRCACHE_SIZE 63

enum movemode_e {
	MODE_MOVE,
	MODE_COPY,
};

struct jump_mark {
	char mark;
	char *path;
};

typedef struct Fm {

	/* Height of the fm in the ui, needed to adjust pos for each dir */
	uint16_t height;

	struct {
		/* Visible directories excluding preview, vector of Dir* */
		cvector_vector_type(Dir *) visible;
		uint16_t length;

		Cache cache;

		/* preview directory, NULL if there is none, e.g. if the cursor is resting on a file */
		Dir *preview;
	} dirs;

	/* List of quickmarks including "'" */
	cvector_vector_type(struct jump_mark) marks;

	struct {
		/* Current selection, as a vector of paths */
		cvector_vector_type(char *) files;
		uint16_t length;

		/* Previous seletction, needed for visual selection mode */
		cvector_vector_type(char *) previous;
	} selection;

	struct {
		/* Copy/move buffer, vector of paths */
		cvector_vector_type(char *) files;
		enum movemode_e mode;
	} load;

	struct {
		bool active;
		uint16_t anchor;
	} visual;
} Fm;

/*
 * Moves to the correct starting directory, loads initial dirs and sets up
 * previews and inotify watchers.
 */
void fm_init(Fm *fm);

/*
 * Unloads directories and frees all resources.
 */
void fm_deinit(Fm *fm);

/*
 * Updates the number of loaded directories, called after the number of columns
 * changes.
 */
void fm_recol(Fm *fm);

/*
 * Current directory. Never `NULL`.
 */
#define fm_current_dir(fm) (fm)->dirs.visible[0]

/*
 * Current file of the current directory. Can be `NULL`.
 */
#define fm_current_file(fm) dir_current_file(fm_current_dir(fm))

#define fm_preview_dir(fm) (fm)->preview

bool fm_cursor_move(Fm *fm, int16_t ct);

static inline void fm_cursor_move_to_ind(Fm *fm, uint16_t ind)
{
	fm_cursor_move(fm, ind - fm_current_dir(fm)->ind);
}

/*
 * Move cursor `ct` up in the current directory.
 */
static inline bool fm_up(Fm *fm, int16_t ct)
{
	return fm_cursor_move(fm, -ct);
}

/*
 * Move cursor `ct` down in the current directory.
 */
static inline bool fm_down(Fm *fm, int16_t ct)
{
	return fm_cursor_move(fm, ct);
}

/*
 * Move cursor to the top of the current directory.
 */
static inline bool fm_top(Fm *fm)
{
	return fm_up(fm, fm_current_dir(fm)->ind);
}

/*
 * Move cursor to the bottom of the current directory.
 */
static inline bool fm_bot(Fm *fm)
{
	return fm_down(fm, fm_current_dir(fm)->length - fm_current_dir(fm)->ind);
}

/*
 * Changes directory to the directory given by `path`. If `save` then the current
 * directory will be saved as the special "'" mark. Returns `true´ if the directory has been changed.
 */
bool fm_chdir(Fm *fm, const char *path, bool save);

/*
 * Open the currently selected file: if it is a directory, chdir into it.
 * Otherwise return the file so that the caller can open it.
 */
File *fm_open(Fm *fm);

/*
 * Chdir to the parent of the current directory.
 */
bool fm_updir(Fm *fm);

/*
 * Move the cursor the file with `name` if it exists. Otherwise leaves the
 * cursor at the closest valid position (i.e. after the number of files
 * decreases)
 */
void fm_move_cursor_to(Fm *fm, const char *name);

/*
 * Move the cursor the file at index `ind`.
 */
void fm_cursor_move_to_ind(Fm *fm, uint16_t ind);

/*
 * Apply the filter string given by `filter` to the current directory.
 */
void fm_filter(Fm *fm, const char *filter);

/*
 * Return the filter string of the currently selected directory.
 */
const char *fm_filter_get(const Fm *fm);

/*
 *  Show hidden files.
 */
void fm_hidden_set(Fm *fm, bool hidden);

/*
 * Checks all visible directories for changes on disk and schedules reloads if
 * necessary.
 */
void fm_check_dirs(const Fm *fm);

/*
 * Chdir to the directory saved as `mark`.
 */
bool fm_mark_load(Fm *fm, char mark);

/*
 * Toggles the selection of the currently selected file.
 */
void fm_selection_toggle_current(Fm *fm);

/*
 * Add `path` to the current selection if not already contained.
 */
void fm_selection_add_file(Fm *fm, const char *path);

/*
 * Replace the current selection.
 */
void fm_selection_set(Fm *fm, cvector_vector_type(char*) selection);

/*
 * Clear the selection completely.
 */
void fm_selection_clear(Fm *fm);

/*
 * Reverse the file selection.
 */
void fm_selection_reverse(Fm *fm);

/*
 * Begin visual selection mode.
 */
void fm_selection_visual_start(Fm *fm);

/*
 * End visual selection mode.
 */
void fm_selection_visual_stop(Fm *fm);

/*
 * Toggle visual selection mode.
 */
void fm_selection_visual_toggle(Fm *fm);

/*
 * Write the current celection to the file given as `path`.
 * Directories are created as needed.
 */
void fm_selection_write(const Fm *fm, const char *path);

/*
 * Move the current selection to the copy buffer.
 */
void fm_copy(Fm *fm);

/*
 * Move the current selection to the move buffer.
 */
void fm_cut(Fm *fm);

/*
 * Clear copy/move buffer.
 */
void fm_load_clear(Fm *fm);

/*
 * Get the list of files in copy/move buffer. Returns a cvector of char*.
 */
char * const* fm_get_load(const Fm *fm);

/*
 * Get the mode current load, one of `MODE_COPY`, `MODE_MOVE`.
 */
enum movemode_e fm_get_mode(const Fm *fm);

/*
 * Compare PATH against each path in SELECTION to check if it is contained.
 */
bool cvector_contains(const char *path, cvector_vector_type(char*) selection);

/*
 * Drop directory cache and reload visible directories from disk.
 */
void fm_drop_cache(Fm *fm);

void fm_reload(Fm *fm);

void fm_update_preview(Fm *fm);

void fm_flatten(Fm *fm, uint8_t level);
