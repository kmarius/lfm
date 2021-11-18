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

static bool file_filtered(file_t *file, const char *filter);
static void apply_filter(dir_t *dir);
static void file_deinit(file_t *file);
static inline bool file_hidden(file_t *file);
static inline void swap(file_t **a, file_t **b);
static void shuffle(void *arr, size_t n, size_t size);
static int file_count(const char *path);
static bool file_load(file_t *file, const char *basedir, const char *name);

file_t *dir_current_file(const dir_t *dir)
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

static bool file_filtered(file_t *file, const char *filter)
{
	return strcasestr(file->name, filter) != NULL;
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
				sizeof(file_t *) * dir->sortedlen);
		dir->len = dir->sortedlen;
	}
	dir->ind = max(min(dir->ind, dir->len - 1), 0);
}

static inline bool file_hidden(file_t *file) {
	return file->name[0] == '.';
}

static void file_deinit(file_t *file)
{
	if (file == NULL) {
		return;
	}
	free(file->path);
	free(file->link_target);
}

static inline void swap(file_t **a, file_t **b)
{
	file_t *t = *a;
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
				qsort(dir->allfiles, dir->alllen, sizeof(file_t),
						compare_natural);
				break;
			case SORT_NAME:
				qsort(dir->allfiles, dir->alllen, sizeof(file_t),
						compare_name);
				break;
			case SORT_SIZE:
				qsort(dir->allfiles, dir->alllen, sizeof(file_t),
						compare_size);
				break;
			case SORT_CTIME:
				qsort(dir->allfiles, dir->alllen, sizeof(file_t),
						compare_ctime);
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
				if (file_isdir(dir->allfiles + i)) {
					dir->sortedfiles[j++] =
						&dir->allfiles[i];
				}
			}
			ndirs = j;
			/* second pass: files */
			for (i = 0; i < dir->alllen; i++) {
				if (!file_isdir(dir->allfiles + i)) {
					dir->sortedfiles[j++] =
						&dir->allfiles[i];
				}
			}
		} else {
			for (i = 0, j = 0; i < dir->alllen; i++) {
				dir->sortedfiles[j++] = &dir->allfiles[i];
			}
		}
	} else {
		j = 0;
		if (dir->dirfirst) {
			for (i = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles + i) &&
						file_isdir(dir->allfiles + i)) {
					dir->sortedfiles[j++] =
						&dir->allfiles[i];
				}
			}
			ndirs = j;
			for (i = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles + i) &&
						!file_isdir(dir->allfiles + i)) {
					dir->sortedfiles[j++] =
						&dir->allfiles[i];
				}
			}
		} else {
			for (i = 0, j = 0; i < dir->alllen; i++) {
				if (!file_hidden(dir->allfiles + i)) {
					dir->sortedfiles[j++] =
						&dir->allfiles[i];
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

bool file_isdir(const file_t *file)
{
	return S_ISDIR(file->stat.st_mode);
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

/* this causes directories that contain slow mountpoints to also load slowly */
/* TODO: check that this doesn't get stuck e.g. on nfs (on 2021-10-25) */
static int file_count(const char *path)
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

static bool file_load(file_t *file, const char *basedir, const char *name)
{
	char buf[PATH_MAX] = {0};
	bool isroot = basedir[0] == '/' && basedir[1] == 0;
	asprintf(&file->path, "%s/%s", isroot ? "" : basedir, name);

	if (lstat(file->path, &file->lstat) == -1) {
		/* likely the file was deleted */
		log_error("lstat: %s", strerror(errno));
		free(file->path);
		return false;
	}

	file->name = basename(file->path);
	file->ext = strrchr(file->name, '.');
	if (file->ext == file->name) {
		/* hidden file, name begins with '.'  */
		file->ext = NULL;
	}
	file->link_target = NULL;
	file->broken = false;

	if (S_ISLNK(file->lstat.st_mode)) {
		if (stat(file->path, &file->stat) == -1) {
			log_error("stat: %s", strerror(errno));
			file->broken = true;
			file->stat = file->lstat;
		}
		if (readlink(file->path, buf, sizeof(buf)) == -1) {
			log_error("readlink: %s", strerror(errno));
			file->broken = true;
		} else {
			file->link_target = strdup(buf);
		}
	} else {
		// for non-symlinks: stat == lstat
		file->stat = file->lstat;
	}

	return true;
}

dir_t *dir_load(const char *path, bool load_filecount)
{
	int i;
	struct dirent *dp;
	file_t f;
	dir_t *dir = new_dir(path);

	DIR *dirp = opendir(path);
	if (dirp == NULL) {
		log_error("opendir: %s", strerror(errno));
		dir->error = errno;
		return dir;
	}

	for (i = 0; (dp = readdir(dirp)) != NULL;) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == 0 ||
				 (dp->d_name[1] == '.' && dp->d_name[2] == 0))) {
			continue;
		}
		if (file_load(&f, path, dp->d_name)) {
			f.filecount = (load_filecount && file_isdir(&f)) ? file_count(f.path) : 0;
			cvector_push_back(dir->allfiles, f);
			i++;
		}
	}
	closedir(dirp);

	dir->alllen = i;

	dir->sortedfiles = malloc(sizeof(file_t*) * dir->alllen);
	dir->files = malloc(sizeof(file_t*) * dir->alllen);

	if (dir->files == NULL || dir->sortedfiles == NULL) {
		dir->error = -1;
		return dir;
	}

	dir->sortedlen = dir->alllen;
	dir->len = dir->alllen;

	for (i = 0; i < dir->alllen; i++) {
		dir->sortedfiles[i] = dir->allfiles + i;
		dir->files[i] = dir->allfiles + i;
	}

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
	for (int i = 0; i < dir->alllen; i++) {
		file_deinit(dir->allfiles+i);
	}
	cvector_free(dir->allfiles);
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
	for (int i = 0; i < dir->alllen; i++) {
		file_deinit(dir->allfiles+i);
	}
	cvector_free(dir->allfiles);
	free(dir->sortedfiles);
	free(dir->files);
	free(dir->sel);
	free(dir->path);
	free(dir);
}
