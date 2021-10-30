#ifndef DIR_H
#define DIR_H

#include <linux/limits.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cvector.h"
#include "sort.h"

enum sorttype_e { SORT_NATURAL, SORT_NAME, SORT_SIZE, SORT_CTIME, };

typedef struct file_t {
	struct stat stat;
	char *path;
	char *name;
	char *ext;
	char *link_target;
	int filecount; /* in case of dir */
} file_t;

typedef struct dir_t {
	char *path;
	char *name;	/* a substring of path */

	cvector_vector_type(file_t) allfiles;    /* files including hidden/filtered */
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
 * Returns true if the file is a directory or, if it is a link, if the link
 * target is one.
 */
bool file_isdir(const file_t *file);

/*
 * Returns true if the file is a executable or, if it is a link, if the link
 * target is.
 */
#define file_isexec(f) ((f)->stat.st_mode & (1 | 8 | 64))

#define file_islink(f) ((f)->link_target != NULL)

/*
 * New directory with a 'loading' marker.
 */
dir_t *dir_new_loading(const char *path);

/*
 * Loads the directory given by PATH from disk.
 */
dir_t *dir_load(const char *path, int filecount);

/*
 * Frees all resources belonging to DIR.
 */
void dir_free(dir_t *dir);

/*
 * Current file of DIR. Can be NULL.
 */
file_t *dir_current_file(const dir_t *dir);

/*
 * Sort DIR with respect to dir->hidden, dir->dirfirst, dir->reverse,
 * dir->sorttype.
 */
void dir_sort(dir_t *dir);

/*
 * Returns the path of the parent of DIR and NULL for the root directory.
 */
const char *dir_parent(const dir_t *dir);

/*
 * Applies the filter string FILTER to DIR.
 */
void dir_filter(dir_t *dir, const char *filter);

/*
 * Check DIR for changes on disk by comparing timestamps. Returns true if there
 * are no changes, false otherwise.
 */
bool dir_check(const dir_t *dir);

#define dir_isroot(d) ((d)->path[0] == '/' && (d)->path[1] == 0)

#endif /* DIR_H */
