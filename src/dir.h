#pragma once

#include <linux/limits.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>

#include "cvector.h"
#include "file.h"
#include "sort.h"

#define FILTER_LEN_MAX 64

enum sorttype_e { SORT_NATURAL, SORT_NAME, SORT_SIZE, SORT_CTIME, SORT_RAND, };

typedef struct {
	char *path;
	char *name;	/* a substring of path */

	File **files_all;    /* files including hidden/filtered */
	File **files_sorted; /* files excluding hidden */
	File **files;	     /* visible files */
	uint16_t length_all;	 /* length of the array of all files */
	uint16_t length_sorted;	 /* length of the array of sorted files */
	uint16_t length;	 /* length of the array of visible files */

	time_t load_time; /* load time, used to check for changes on disk and
			    reload if necessary */
	int16_t error;	 /* for now, true if any error occurs when loading */
	bool loading;

	uint16_t ind;	 /* index of currently selected file */
	uint16_t pos;	 /* position of the cursor in fm */
	char *sel;

	char filter[FILTER_LEN_MAX]; /* filter string */

	bool sorted;
	bool hidden;
	bool dirfirst;
	bool reverse;
	enum sorttype_e sorttype;
} Dir;

/*
 * New directory with a 'loading' marker.
 */
Dir *dir_new_loading(const char *path);

/*
 * Loads the directory at `path` from disk. Count the files in each
 * subdirectory if `load_filecount` is `true`.
 */
Dir *dir_load(const char *path, bool load_filecount);

/*
 * Free all resources belonging to DIR.
 */
void dir_destroy(Dir *dir);

/*
 * Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
 * if files are filtered/hidden.
 */
File *dir_current_file(const Dir *dir);

/*
 * Sort `dir` with respect to `dir->hidden`, `dir->dirfirst`, `dir->reverse`,
 * `dir->sorttype`.
 */
void dir_sort(Dir *dir);

/*
 * Returns the path of the parent of `dir` and `NULL` for the root directory.
 */
const char *dir_parent_path(const Dir *dir);

/*
 * Applies the filter string `filter` to `dir`. `NULL` or `""` clears the filter.
 * Only up to `FILTER_LEN_MAX` characters are used.
 */
void dir_filter(Dir *dir, const char *filter);

/*
 * Check `dir` for changes on disk by comparing mtime. Returns `true` if there
 * are no changes, `false` otherwise.
 */
bool dir_check(const Dir *dir);

/*
 * Move the cursor in the current dir by `ct`, respecting the `scrolloff`
 * setting by passing it and the current `height` of the viewport.
 */
void dir_cursor_move(Dir *dir, int16_t ct, uint16_t height, uint16_t scrolloff);

/*
 * Move the cursor in the current dir to the file `name`, respecting the
 * `scrolloff` setting by passing it and the current `height` of the viewport.
 */
void dir_cursor_move_to(Dir *dir, const char *name, uint16_t height, uint16_t scrolloff);

/*
 * Replace files and metadata of `dir` with those of `update`. Frees `update`.
 */
void dir_update_with(Dir *dir, Dir *update, uint16_t height, uint16_t scrolloff);

/*
 * Returns true `d` is the root directory.
 */
inline bool dir_isroot(const Dir *dir)
{
	return (dir->path[0] == '/' && dir->path[1] == 0);
}
