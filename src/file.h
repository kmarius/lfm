#pragma once

#include <stdbool.h>
#include <sys/stat.h>

typedef struct {
	char *path;
	char *name;
	char *ext;
	struct stat lstat;
	struct stat stat;
	char *link_target;
	bool broken;
	int filecount; /* in case of dir */
} file_t;

file_t *file_create(const char *basedir, const char *name);

void file_destroy(file_t *file);

/*
 * Returns `true` if the file is a directory or, if it is a link, if the link
 * target is one.
 */
bool file_isdir(const file_t *file);

/*
 * Returns `true` if the file is a executable or, if it is a link, if the link
 * target is.
 */
#define file_isexec(f) ((f)->lstat.st_mode & (1 | 8 | 64))

/*
 * Returns `true` if the file is a symbolic link.
 */
#define file_islink(f) ((f)->link_target != NULL)
