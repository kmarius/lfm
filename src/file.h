#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

typedef struct {
	char *path;
	char *name;
	char *ext;
	struct stat lstat;
	struct stat stat;
	char *link_target;
	bool broken;
	int16_t filecount; /* in case of dir */
} File;

File *file_init(File *file, const char *dir, const char *name);

File *file_create(const char *dir, const char *name);

void file_deinit(File *file);

void file_destroy(File *file);

/*
 * Returns `true` if the file is a directory or, if it is a link, if the link
 * target is one.
 */
inline bool file_isdir(const File *file)
{
	return S_ISDIR(file->stat.st_mode);
}

/*
 * Returns `true` if the file is a executable or, if it is a link, if the link
 * target is.
 */
inline bool file_isexec(const File *file)
{
	return file->lstat.st_mode & (1 | 8 | 64);
}

/*
 * Returns `true` if the file is a symbolic link.
 */
inline bool file_islink(const File *file)
{
	return file->link_target != 0;
}
