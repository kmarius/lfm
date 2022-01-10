#define _GNU_SOURCE
#include <errno.h>
#include <linux/limits.h> // PATH_MAX
#include <stdio.h> // asprintf
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // readlink

#include "file.h"

#define T file_t

T *file_create(const char *dir, const char *name)
{
	T *t = malloc(sizeof(T));
	char buf[PATH_MAX] = {0};
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
	t->link_target = NULL;
	t->broken = false;
	t->filecount = 0;

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

void file_destroy(T *t)
{
	if (t == NULL) {
		return;
	}
	free(t->path);
	free(t->link_target);
	free(t);
}

bool file_isdir(const T *file)
{
	return S_ISDIR(file->stat.st_mode);
}

#undef T
