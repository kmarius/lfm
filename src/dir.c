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
#include "sort.h"
#include "util.h"

#define T Dir

#define DIR_INITIALIZER ((T){ \
		.dirfirst = true, \
		.sorttype = SORT_NATURAL, \
		})

File *dir_current_file(const T *t)
{
	if (!t || t->ind >= t->length)
		return NULL;

	return t->files[t->ind];
}


const char *dir_parent_path(const T *t)
{
	if (dir_isroot(t))
		return NULL;

	static char tmp[PATH_MAX + 1];
	strcpy(tmp, t->path);
	return dirname(tmp);
}


static void apply_filter(T *t)
{
	if (t->filter) {
		uint32_t j = 0;
		for (uint32_t i = 0; i < t->length_sorted; i++) {
			if (filter_match(t->filter, file_name(t->files_sorted[i])))
				t->files[j++] = t->files_sorted[i];
		}
		t->length = j;
	} else {
		/* TODO: try to select previously selected file
		 * note that on the first call dir->files is not yet valid */
		memcpy(t->files, t->files_sorted, t->length_sorted * sizeof(File *));
		t->length = t->length_sorted;
	}
	t->ind = max(min(t->ind, t->length - 1), 0);
}


static inline void swap(File **a, File **b)
{
	File *t = *a;
	*a = *b;
	*b = t;
}

// https://stackoverflow.com/questions/6127503/shuffle-array-in-c
// arrange the N elements of ARRAY in random order.
// Only effective if N is much smaller than RAND_MAX;
// if this may not be the case, use a better random
// number generator.
static void shuffle(void *arr, size_t n, size_t size)
{
	if (n <= 1)
		return;

	const size_t stride = size * sizeof(char);
	char *tmp = malloc(n * size);

	size_t i;
	for (i = 0; i < n - 1; ++i) {
		const size_t rnd = (size_t) rand();
		size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

		memcpy(tmp, arr + j * stride, size);
		memcpy(arr + j * stride, arr + i * stride, size);
		memcpy(arr + i * stride, tmp, size);
	}

	free(tmp);
}


/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(T *t)
{
	if (!t->sorted) {
		switch (t->sorttype) {
			case SORT_NATURAL:
				qsort(t->files_all, t->length_all, sizeof(File *), compare_natural);
				break;
			case SORT_NAME:
				qsort(t->files_all, t->length_all, sizeof(File *), compare_name);
				break;
			case SORT_SIZE:
				qsort(t->files_all, t->length_all, sizeof(File *), compare_size);
				break;
			case SORT_CTIME:
				qsort(t->files_all, t->length_all, sizeof(File *), compare_ctime);
				break;
			case SORT_RAND:
				shuffle(t->files_all, t->length_all, sizeof(File *));
			default:
				break;
		}
		t->sorted = 1;
	}
	uint32_t ndirs = 0;
	uint32_t j = 0;
	if (t->hidden) {
		if (t->dirfirst) {
			/* first pass: directories */
			for (uint32_t i = 0; i < t->length_all; i++) {
				if (file_isdir(t->files_all[i]))
					t->files_sorted[j++] = t->files_all[i];
			}
			ndirs = j;
			/* second pass: files */
			for (uint32_t i = 0; i < t->length_all; i++) {
				if (!file_isdir(t->files_all[i]))
					t->files_sorted[j++] = t->files_all[i];
			}
		} else {
			j = t->length_all;
			memcpy(t->files_sorted, t->files, t->length_all * sizeof(File *));
		}
	} else {
		if (t->dirfirst) {
			for (uint32_t i = 0; i < t->length_all; i++) {
				if (!file_hidden(t->files_all[i]) && file_isdir(t->files_all[i]))
					t->files_sorted[j++] = t->files_all[i];
			}
			ndirs = j;
			for (uint32_t i = 0; i < t->length_all; i++) {
				if (!file_hidden(t->files_all[i]) && !file_isdir(t->files_all[i]))
					t->files_sorted[j++] = t->files_all[i];
			}
		} else {
			for (uint32_t i = 0; i < t->length_all; i++) {
				if (!file_hidden(t->files_all[i]))
					t->files_sorted[j++] = t->files_all[i];
			}
		}
	}
	t->length_sorted = j;
	t->length = j;
	if (t->reverse) {
		for (uint32_t i = 0; i < ndirs / 2; i++)
			swap(t->files_sorted+i, t->files_sorted + ndirs - i - 1);
		for (uint32_t i = 0; i < (t->length_sorted - ndirs) / 2; i++)
			swap(t->files_sorted + ndirs + i, t->files_sorted + t->length_sorted - i - 1);
	}

	apply_filter(t);
}


void dir_filter(T *t, const char *filter)
{
	if (t->filter) {
		filter_destroy(t->filter);
		t->filter = NULL;
	}

	if (filter && filter[0] != 0)
		t->filter = filter_create(filter);

	apply_filter(t);
}


bool dir_check(const T *t)
{
	struct stat statbuf;
	if (stat(t->path, &statbuf) == -1) {
		log_error("stat: %s", strerror(errno));
		return false;
	}
	return statbuf.st_mtime <= t->load_time;
}


T *dir_init(T *t, const char *path)
{
	*t = DIR_INITIALIZER;

	if (path[0] != '/') {
		char buf[PATH_MAX + 1];
		realpath(path, buf);
		t->path = strdup(buf);
	} else {
		/* to preserve symlinks we don't use realpath */
		t->path = strdup(path);
	}

	t->load_time = time(NULL);
	t->name = basename(t->path);
	t->next = current_millis();

	return t;
}


T *dir_load(const char *path, bool load_dircount)
{
	struct dirent *dp;
	T *dir = dir_create(path);
	dir->dircounts = load_dircount;

	DIR *dirp = opendir(path);
	if (!dirp) {
		log_error("opendir: %s", strerror(errno));
		dir->error = errno;
		return dir;
	}

	while ((dp = readdir(dirp))) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == 0 ||
				 (dp->d_name[1] == '.' && dp->d_name[2] == 0)))
			continue;

		File *file = file_create(path, dp->d_name);
		if (file) {
			if (load_dircount && file_isdir(file))
				file->dircount = file_dircount_load(file);

			cvector_push_back(dir->files_all, file);
		}
	}
	closedir(dirp);

	dir->length_all = cvector_size(dir->files_all);
	dir->length_sorted = dir->length_all;
	dir->length = dir->length_all;

	dir->files_sorted = malloc(dir->length_all * sizeof(File *));
	dir->files = malloc(dir->length_all * sizeof(File *));

	memcpy(dir->files_sorted, dir->files_all, dir->length_all * sizeof(File *));
	memcpy(dir->files, dir->files_all, dir->length_all * sizeof(File *));
	dir->updates = 1;

	return dir;
}

struct queue_dirs_node {
	const char *path;
	uint8_t level;
	bool hidden;
	struct queue_dirs_node *next;
};

struct queue_dirs {
	struct queue_dirs_node *head;
	struct queue_dirs_node *tail;
};


T *dir_load_flat(const char *path, uint8_t level, bool load_dircount)
{
	uint64_t t0 = current_millis();

	T *dir = dir_create(path);
	dir->dircounts = load_dircount;
	dir->flatten_level = level;

	struct queue_dirs queue;
	queue.head = malloc(sizeof(*queue.head));
	queue.head->path = path;
	queue.head->level = 0;
	queue.head->next = NULL;
	queue.head->hidden = false;

	while (queue.head) {
		struct queue_dirs_node *head = queue.head;
		queue.head = head->next;
		if (!queue.head)
			queue.tail = NULL;

		DIR *dirp = opendir(head->path);
		if (!dirp)
			goto cont;

		struct dirent *dp;
		while ((dp = readdir(dirp))) {
			if (dp->d_name[0] == '.' &&
					(dp->d_name[1] == 0 ||
					 (dp->d_name[1] == '.' && dp->d_name[2] == 0)))
				continue;

			File *file = file_create(head->path, dp->d_name);
			if (file) {
				file->hidden |= head->hidden;
				if (file_isdir(file)) {
					if (load_dircount)
						file->dircount = file_dircount_load(file);

					if (head->level + 1 <= level) {
						struct queue_dirs_node *n = malloc(sizeof(*n));
						n->path = file_path(file);
						n->level = head->level + 1;
						n->next = NULL;
						n->hidden = file_hidden(file);
						if (!queue.head) {
							queue.head = n;
							queue.tail = n;
						} else {
							queue.tail->next = n;
							queue.tail = n;
						}
					}
				}
				for (uint8_t i = 0; i < head->level; i++) {
					file->name -= 2;
					while (*(file->name-1) != '/')
						file->name--;
				}

				cvector_push_back(dir->files_all, file);
			}
		}
		closedir(dirp);
cont:
		free(head);
	}

	dir->length_all = cvector_size(dir->files_all);
	dir->length_sorted = dir->length_all;
	dir->length = dir->length_all;

	dir->files_sorted = malloc(dir->length_all * sizeof(File *));
	dir->files = malloc(dir->length_all * sizeof(File *));

	memcpy(dir->files_sorted, dir->files_all, dir->length_all * sizeof(File *));
	memcpy(dir->files, dir->files_all, dir->length_all * sizeof(File *));

	log_debug("flat dir %s loaded in %ums", path, current_millis() - t0);

	return dir;
}


void dir_cursor_move(T *t, int32_t ct, uint16_t height, uint16_t scrolloff)
{
	t->ind = max(min(t->ind + ct, t->length - 1), 0);
	if (ct < 0)
		t->pos = min(max(scrolloff, t->pos + ct), t->ind);
	else
		t->pos = max(min(height - 1 - scrolloff, t->pos + ct), height - t->length + t->ind);
}


void dir_cursor_move_to(T *t, const char *name, uint16_t height, uint16_t scrolloff)
{
	if (!name)
		return;

	if (!t->files) {
		free(t->sel);
		t->sel = strdup(name);
		return;
	}

	for (uint32_t i = 0; i < t->length; i++) {
		if (streq(file_name(t->files[i]), name)) {
			dir_cursor_move(t, i - t->ind, height, scrolloff);
			return;
		}
	}
	t->ind = min(t->ind, t->length);
}


void dir_update_with(T *t, Dir *update, uint16_t height, uint16_t scrolloff)
{
	if (!t->sel && t->ind < t->length)
		t->sel = strdup(file_name(t->files[t->ind]));

	cvector_ffree(t->files_all, file_destroy);
	free(t->files_sorted);
	free(t->files);

	t->files_all = update->files_all;
	t->files_sorted = update->files_sorted;
	t->files = update->files;
	t->length_all = update->length_all;
	t->load_time = update->load_time;
	t->error = update->error;
	t->flatten_level = update->flatten_level;

	t->updates++;

	free(update->sel);
	free(update->path);
	free(update);

	t->sorted = false;
	dir_sort(t);

	if (t->sel) {
		dir_cursor_move_to(t, t->sel, height, scrolloff);
		free(t->sel);
		t->sel = NULL;
	}
}


void dir_deinit(T *t)
{
	if (!t)
		return;

	cvector_ffree(t->files_all, file_destroy);
	filter_destroy(t->filter);
	free(t->files_sorted);
	free(t->files);
	free(t->sel);
	free(t->path);
}
