#define _GNU_SOURCE
#include <errno.h>
#include <linux/limits.h> // PATH_MAX
#include <stdio.h> // asprintf
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // readlink

#include "file.h"

#define T File

T *file_init(T *t, const char *dir, const char *name)
{
	char buf[PATH_MAX] = {0};
	*t = (T) { .filecount = -1, };

	bool isroot = dir[1] == 0 && dir[0] == '/';
	asprintf(&t->path, "%s/%s", isroot ? "" : dir, name);

	if (lstat(t->path, &t->lstat) == -1) {
		/* likely the file was deleted */
		free(t->path);
		free(t);
		return NULL;
	}

	t->name = basename(t->path);
	t->ext = strrchr(t->name, '.');
	if (t->ext == t->name) {
		/* hidden file */
		t->ext = NULL;
	}

	if (S_ISLNK(t->lstat.st_mode)) {
		if (stat(t->path, &t->stat) == -1) {
			t->broken = true;
			t->stat = t->lstat;
		}
		if (readlink(t->path, buf, sizeof(buf)) == -1) {
			t->broken = true;
		} else {
			t->link_target = strdup(buf);
		}
	} else {
		// for non-symlinks stat == lstat
		t->stat = t->lstat;
	}

	return t;
}

T *file_create(const char *dir, const char *name)
{
	return file_init(malloc(sizeof(T)), dir, name);
}

void file_deinit(T *t)
{
	if (t == NULL) {
		return;
	}
	free(t->path);
	free(t->link_target);
}

void file_destroy(T *t)
{
	file_deinit(t);
	free(t);
}

bool file_isdir(const T *t);
bool file_islink(const T *t);
bool file_isexec(const T *t);

#undef T
