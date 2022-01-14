#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "util.h"

static void apply_filter(dir_t *dir);
static inline void swap(File **a, File **b);
static void shuffle(void *arr, size_t n, size_t size);
static int get_file_count(const char *path);

File *dir_current_file(const dir_t *dir)
{
	if (dir == NULL || dir->ind >= dir->len) {
		return NULL;
	}
	return dir->files[dir->ind];
}

const char *dir_parent(const dir_t *dir)
{
	static char tmp[PATH_MAX + 1];
	if (streq(dir->path, "/")) {
		return NULL;
	}
	strcpy(tmp, dir->path);
	return dirname(tmp);
}

static bool file_filtered(File *file, const char *filter)
{
	return strcasestr(file->name, filter) != NULL;
}

static inline bool file_hidden(File *file) {
	return file->name[0] == '.';
}

static void apply_filter(dir_t *dir)
{
	int i, j = 0;
	if (dir->filter[0] != 0) {
		for (i = 0; i < dir->sortedlen; i++) {
			if (file_filtered(dir->sortedfiles[i], dir->filter)) {
				dir->files[j++] = dir->sortedfiles[i];
			}
		}
		dir->len = j;
	} else {
		/* TODO: try to select previously selected file
		 * note that on the first call dir->files is not yet valid */
		memcpy(dir->files, dir->sortedfiles,
				sizeof(File *) * dir->sortedlen);
		dir->len = dir->sortedlen;
	}
	dir->ind = max(min(dir->ind, dir->len - 1), 0);
}

static inline void swap(File **a, File **b)
{
	File *t = *a;
	*a = *b;
	*b = t;
}

// https://stackoverflow.com/questions/6127503/shuffle-array-in-c
/* arrange the N elements of ARRAY in random order.
 * Only effective if N is much smaller than RAND_MAX;
 * if this may not be the case, use a better random
 * number generator. */
static void shuffle(void *arr, size_t n, size_t size)
{
	char tmp[size];
	size_t stride = size * sizeof(char);

	if (n > 1) {
		size_t i;
		for (i = 0; i < n - 1; ++i) {
			size_t rnd = (size_t) rand();
			size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

			memcpy(tmp, arr + j * stride, size);
			memcpy(arr + j * stride, arr + i * stride, size);
			memcpy(arr + i * stride, tmp, size);
		}
	}
}

/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(dir_t *dir)
{
	if (!dir->sorted) {
		switch (dir->sorttype) {
			case SORT_NATURAL:
				qsort(dir->allfiles, dir->alllen, sizeof(File*), compare_natural);
				break;
			case SORT_NAME:
				qsort(dir->allfiles, dir->alllen, sizeof(File*), compare_name);
				break;
			case SORT_SIZE:
				qsort(dir->allfiles, dir->alllen, sizeof(File*), compare_size);
				break;
			case SORT_CTIME:
				qsort(dir->allfiles, dir->alllen, sizeof(File*), compare_ctime);
				break;
			case SORT_RAND:
				shuffle(dir->allfiles, dir->alllen, sizeof(*dir->allfiles));
			default:
				break;
		}
		dir->sorted = 1;
	}
	int i, j, ndirs = 0;
	if (dir->hidden) {
		/* first pass: directories */
		j = 0;
		if (dir->dirfirst) {
			for (i = 0; i < dir->alllen; i++) {
				if (file_isdir(dir->allfiles[i])) {
					dir->sortedfiles[j++] = dir->allfiles[i];
				}
			}
			ndirs = j;
			/* second pass: files */
			for (i = 0; i < dir->alllen; i++) {
				if (!file_isdir(dir->allfiles[i])) {
					dir->sortedfiles[j++] = dir->allfiles[i];
				}
			}
		} else {
			for (i = 0, j = 0; i < dir->alllen; i++) {
				dir->sortedfiles[j++] = dir->allfiles[i];
			}
		}
	} else {
		j = 0;
		if (dir->dirfirst) {
			for (i = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles[i]) &&
						file_isdir(dir->allfiles[i])) {
					dir->sortedfiles[j++] = dir->allfiles[i];
				}
			}
			ndirs = j;
			for (i = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles[i]) &&
						!file_isdir(dir->allfiles[i])) {
					dir->sortedfiles[j++] = dir->allfiles[i];
				}
			}
		} else {
			for (i = 0, j = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles[i])) {
					dir->sortedfiles[j++] = dir->allfiles[i];
				}
			}
		}
	}
	dir->sortedlen = j;
	dir->len = j;
	if (dir->reverse) {
		for (i = 0; i < ndirs / 2; i++) {
			swap(dir->sortedfiles+i,
					dir->sortedfiles + ndirs - i - 1);
		}
		for (i = 0; i < (dir->sortedlen - ndirs) / 2; i++) {
			swap(dir->sortedfiles + ndirs + i,
					dir->sortedfiles + dir->sortedlen - i - 1);
		}
	}

	apply_filter(dir);
}

void dir_filter(dir_t *dir, const char *filter)
{
	if (filter == NULL) {
		filter = "";
	}
	strncpy(dir->filter, filter, sizeof(dir->filter));
	dir->filter[sizeof(dir->filter)-1] = 0;
	apply_filter(dir);
}

bool dir_check(const dir_t *dir)
{
	struct stat statbuf;
	if (stat(dir->path, &statbuf) == -1) {
		log_error("stat: %s", strerror(errno));
		return false;
	}
	return statbuf.st_mtime <= dir->loadtime;
}

dir_t *new_dir(const char *path)
{
	dir_t *dir = malloc(sizeof(dir_t));

	if (path[0] != '/') {
		char buf[PATH_MAX + 1];
		realpath(path, buf);
		dir->path = strdup(buf);
	} else {
		/* to preserve symlinks we don't use realpath */
		dir->path = strdup(path);
	}

	dir->allfiles = NULL;
	dir->alllen = 0;
	dir->dirfirst = true;
	dir->error = 0;
	dir->files = NULL;
	dir->filter[0] = 0;
	dir->hidden = false;
	dir->ind = 0;
	dir->len = 0;
	dir->loadtime = time(NULL);
	dir->name = basename(dir->path);
	dir->pos = 0;
	dir->reverse = false;
	dir->sel = NULL;
	dir->sorted = false;
	dir->sortedfiles = NULL;
	dir->sortedlen = 0;
	dir->sorttype = SORT_NATURAL;
	dir->loading = false;

	return dir;
}

static int get_file_count(const char *path)
{
	int ct;
	struct dirent *dp;

	DIR *dirp = opendir(path);
	if (dirp == NULL) {
		return 0;
	}

	for (ct = 0; (dp = readdir(dirp)) != NULL; ct++) ;
	closedir(dirp);
	return ct - 2;
}

dir_t *dir_new_loading(const char *path)
{
	dir_t *d = new_dir(path);
	d->loading = true;
	return d;
}

dir_t *dir_load(const char *path, bool load_filecount)
{
	struct dirent *dp;
	dir_t *dir = new_dir(path);

	DIR *dirp = opendir(path);
	if (dirp == NULL) {
		log_error("opendir: %s", strerror(errno));
		dir->error = errno;
		return dir;
	}

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == 0 ||
				 (dp->d_name[1] == '.' && dp->d_name[2] == 0))) {
			continue;
		}
		File *file = file_create(path, dp->d_name);
		if (file != NULL) {
			if (load_filecount && file_isdir(file)) {
				file->filecount = get_file_count(file->path);
			}
			cvector_push_back(dir->allfiles, file);
		}
	}
	closedir(dirp);

	dir->alllen = cvector_size(dir->allfiles);
	dir->sortedlen = dir->alllen;
	dir->len = dir->alllen;

	dir->sortedfiles = malloc(sizeof(File*) * dir->alllen);
	dir->files = malloc(sizeof(File*) * dir->alllen);

	memcpy(dir->sortedfiles, dir->allfiles, sizeof(File*)*dir->alllen);
	memcpy(dir->files, dir->allfiles, sizeof(File*)*dir->alllen);

	return dir;
}

void dir_cursor_move(dir_t *dir, int ct, int height, int scrolloff)
{
	dir->ind = max(min(dir->ind + ct, dir->len - 1), 0);
	if (ct < 0) {
		dir->pos = min(max(scrolloff, dir->pos + ct), dir->ind);
	} else {
		dir->pos = max(min(height - 1 - scrolloff, dir->pos + ct), height - dir->len + dir->ind);
	}
}

void dir_cursor_move_to(dir_t *dir, const char *name, int height, int scrolloff)
{
	int i;
	if (name == NULL) {
		return;
	}
	if (dir->files == NULL) {
		free(dir->sel);
		dir->sel = strdup(name);
		return;
	}
	for (i = 0; i < dir->len; i++) {
		if (streq(dir->files[i]->name, name)) {
			dir_cursor_move(dir, i - dir->ind, height, scrolloff);
			return;
		}
	}
	dir->ind = min(dir->ind, dir->len);
}

void dir_update_with(dir_t *dir, dir_t *update, int height, int scrolloff)
{
	if (dir->sel == NULL && dir->ind < dir->len) {
		dir->sel = strdup(dir->files[dir->ind]->name);
	}
	cvector_ffree(dir->allfiles, file_destroy);
	free(dir->sortedfiles);
	free(dir->files);

	dir->allfiles = update->allfiles;
	dir->sortedfiles = update->sortedfiles;
	dir->files = update->files;
	dir->alllen = update->alllen;
	dir->loadtime = update->loadtime;
	dir->loading = update->loading;
	dir->error = update->error;

	free(update->sel);
	free(update->path);
	free(update);

	dir->sorted = false;
	dir_sort(dir);

	if (dir->sel != NULL) {
		dir_cursor_move_to(dir, dir->sel, height, scrolloff);
		free(dir->sel);
		dir->sel = NULL;
	}
}

void dir_free(dir_t *dir)
{
	if (dir == NULL) {
		return;
	}
	cvector_ffree(dir->allfiles, file_destroy);
	free(dir->sortedfiles);
	free(dir->files);
	free(dir->sel);
	free(dir->path);
	free(dir);
}
