#pragma once

#include <linux/limits.h>
#include <stdbool.h>
#include <sys/types.h>

#include "cvector.h"
#include "file.h"
#include "sort.h"

enum sorttype_e { SORT_NATURAL, SORT_NAME, SORT_SIZE, SORT_CTIME, SORT_RAND, };

typedef struct dir_t {
	char *path;
	char *name;	/* a substring of path */

	cvector_vector_type(file_t*) allfiles;    /* files including hidden/filtered */
	file_t **sortedfiles; /* files excluding hidden */
	file_t **files;	     /* visible files */
	int alllen;	 /* length of the array of all files */
	int sortedlen;	 /* length of the array of sorted files */
	int len;	 /* length of the array of visible files */

	time_t loadtime; /* load time, used to check for changes on disk and
			    reload if necessary */
	int error;	 /* for now, true if any error occurs when loading */
	bool loading : 1;

	int ind;	 /* index of currently selected file */
	int pos;	 /* position of the cursor in fm */
	char *sel;

	char filter[64]; /* filter string */

	bool sorted : 1;
	bool hidden : 1; /* show hidden files */
	bool dirfirst : 1;
	bool reverse : 1;
	enum sorttype_e sorttype;
} dir_t;

/*
 * New directory with a 'loading' marker.
 */
dir_t *dir_new_loading(const char *path);

/*
 * Loads the directory at `path` from disk. Count the files in each
 * subdirectory if `load_filecount` is `true`.
 */
dir_t *dir_load(const char *path, bool load_filecount);

/*
 * Free all resources belonging to DIR.
 */
void dir_free(dir_t *dir);

/*
 * Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
 * if files are filtered/hidden.
 */
file_t *dir_current_file(const dir_t *dir);

/*
 * Sort `dir` with respect to `dir->hidden`, `dir->dirfirst`, `dir->reverse`,
 * `dir->sorttype`.
 */
void dir_sort(dir_t *dir);

/*
 * Returns the path of the parent of `dir` and `NULL` for the root directory.
 */
const char *dir_parent(const dir_t *dir);

/*
 * Applies the filter string `filter` to `dir`. `NULL` or "" clears the filter.
 */
void dir_filter(dir_t *dir, const char *filter);

/*
 * Check `dir` for changes on disk by comparing mtime. Returns `true` if there
 * are no changes, `false` otherwise.
 */
bool dir_check(const dir_t *dir);

/*
 * Returns true `d` is the root directory.
 */
#define dir_isroot(d) ((d)->path[0] == '/' && (d)->path[1] == 0)

/*
 * Move the cursor in the current dir by `ct`, respecting the `scrolloff`
 * setting by passing it and the current `height` of the viewport.
 */
void dir_cursor_move(dir_t *dir, int ct, int height, int scrolloff);

/*
 * Move the cursor in the current dir to the file `name`, respecting the
 * `scrolloff` setting by passing it and the current `height` of the viewport.
 */
void dir_cursor_move_to(dir_t *dir, const char *name, int height, int scrolloff);

/*
 * Replace files and metadata of `dir` with those of `update`. Frees `update`.
 */
void dir_update_with(dir_t *dir, dir_t *update, int height, int scrolloff);
