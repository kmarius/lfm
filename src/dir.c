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

file_t *dir_current_file(const dir_t *dir)
{
	if (!dir || dir->ind >= dir->len) {
		return NULL;
	}
	return dir->files[dir->ind];
}

void dir_sel(dir_t *dir, const char *file)
{
	/* TODO: Allow NULL for now to reset ind and pos (on 2021-08-01) */
	if (!file) {
		dir->ind = min(dir->ind, dir->len);
		dir->pos = dir->ind;
		log_error("dir_sel: selecting NULL %s", dir->path);
		return;
	}
	if (!dir->files) {
		/* This happens because we are lazily loading dirs. Leave this
		 * marker and select in the actual dir later */
		dir->sel = strdup(file);
		return;
	}
	for (int i = 0; i < dir->len; i++) {
		if (streq(dir->files[i]->name, file)) {
			if (i == dir->ind) {
				return;
			}
			/* TODO: this cant be right, dir->pos should be limited by nav height (on 2021-08-09) */
			dir->ind = i;
			dir->pos = dir->ind;
			return;
		}
	}
	dir->ind = min(dir->ind, dir->len);
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
	int i, j;
	if (dir->filter[0] != 0) {
		for (i = 0, j = 0; i < dir->sortedlen; i++) {
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

static bool file_hidden(file_t *file) { return file->name[0] == '.'; }

static inline void swap(file_t **a, file_t **b)
{
	file_t *t = *a;
	*a = *b;
	*b = t;
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
	dir->sortedlen = j;
	dir->len = j;

	apply_filter(dir);
}

void dir_filter(dir_t *dir, const char *filter)
{
	strncpy(dir->filter, filter, sizeof(dir->filter));
	dir->filter[sizeof(dir->filter)-1] = 0;
	apply_filter(dir);
}

/* TODO: make isdir a field? (on 2021-08-23) */
/* TODO: returns errors on broken symlinks (on 2021-08-23) */
bool file_isdir(const file_t *file)
{
	struct stat statbuf;
	int mode = file->stat.st_mode;
	if (S_ISLNK(mode)) {
		if (stat(file->path, &statbuf) == -1) {
			log_error("%s", strerror(errno));
			return false;
		}
		return S_ISDIR(statbuf.st_mode);
	}
	return S_ISDIR(mode);
}

bool dir_check(const dir_t *dir)
{
	struct stat statbuf;
	if (stat(dir->path, &statbuf) == -1) {
		log_error("%s", strerror(errno));
		return false;
	}
	return statbuf.st_mtime <= dir->loadtime;
}

dir_t *new_dir(const char *path)
{
	dir_t *dir = malloc(sizeof(dir_t));

	if (path[0] != '/') {
		realpath(path, dir->path);
	} else {
		/* to preserve symlinks we don't use realpath */
		/* TODO: deal with ~, ., .. at some point (on 2021-07-20) */
		strncpy(dir->path, path, sizeof(dir->path)-1);
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
	dir->access = 0;

	return dir;
}

/* this causes directories that contain slow mountpoints to also load slowly */
/* TODO: check that this doesn't get stuck e.g. on nfs (on 2021-10-25) */
static int file_count(const char *path)
{
	int ct;
	DIR *dirp;
	struct dirent *dp;

	if (!(dirp = opendir(path))) {
		return 0;
	}

	for (ct = 0; (dp = readdir(dirp)); ct++) ;
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

	if (lstat(file->path, &file->stat) == -1) {
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

	if (S_ISLNK(file->stat.st_mode)) {
		if (readlink(file->path, buf, sizeof(buf)) == -1) {
			log_error("readlink: %s", strerror(errno));
			/* TODO: mark broken symlink? (on 2021-08-02) */
		} else {
			file->link_target = strdup(buf);
		}
	}

	return true;
}

dir_t *dir_load(const char *path, int filecount)
{
	int i;
	struct dirent *dp;
	dir_t *dir = new_dir(path);
	file_t f;

	DIR *dirp;
	if (!(dirp = opendir(path))) {
		log_error("opendir: %s", strerror(errno));
		dir->error = errno;
		return dir;
	}

	for (i = 0; (dp = readdir(dirp));) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == 0 ||
				 (dp->d_name[1] == '.' && dp->d_name[2] == 0))) {
			continue;
		}
		if (file_load(&f, path, dp->d_name)) {
			f.filecount = (filecount && file_isdir(&f)) ? file_count(f.path) : 0;
			cvector_push_back(dir->allfiles, f);
			i++;
		}
	}
	closedir(dirp);

	dir->alllen = i;

	dir->sortedfiles = malloc(sizeof(file_t*) * dir->alllen);
	dir->files = malloc(sizeof(file_t*) * dir->alllen);

	if (!dir->files || !dir->sortedfiles) {
		log_error("load_dir fucked up, malloc failed");
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

void dir_free(dir_t *dir)
{
	if (dir) {
		for (int i = 0; i < dir->alllen; i++) {
			free(dir->allfiles[i].path);
			free(dir->allfiles[i].link_target);
		}
		cvector_free(dir->allfiles);
		free(dir->sortedfiles);
		free(dir->files);
		free(dir->sel);
		free(dir);
	}
}
