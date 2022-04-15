#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "util.h"

typedef struct File {
	char *path;
	char *name;
	char *ext;
	struct stat lstat;
	struct stat stat;
	char *link_target;
	bool isbroken;
	bool isexec;
	bool hidden;
	int32_t dircount; // in case of a directory, < 0 if not loaded yet
	int error;
} File;

File *file_init(File *file, const char *dir, const char *name);

File *file_create(const char *dir, const char *name);

void file_deinit(File *file);

void file_destroy(File *file);

// Returns the full path of the file.
static inline const char *file_path(const File *file)
{
	return file->path;
}

// Returns the name of the file.
static inline const char *file_name(const File *file)
{
	return file->name;
}

// Returns the extension of the file, `NULL` if it does not have one.
static inline const char *file_ext(const File *file)
{
	return file->ext;
}

// Returns the target of a (non-broken) symbolic link, `NULL` otherwise.
static inline const char *file_link_target(const File *file)
{
	return file->link_target;
}

// Returns `true` if the file is a directory or, if it is a link, if the link
// target is one.
static inline bool file_isdir(const File *file)
{
	return S_ISDIR(file->stat.st_mode);
}

// Returns `true` if the file is a executable or, if it is a link, if the link
// target is.
static inline bool file_isexec(const File *file)
{
	return file->isexec;
}

// Returns `true` if the file is a symbolic link.
static inline bool file_islink(const File *file)
{
	return file->link_target != NULL;
}

// Returns `true` if the file is a broken symbolic link.
static inline bool file_isbroken(const File *file)
{
	return file->isbroken;
}

// Returns the number of files in the directory `file`. A negative
// number is returned when the count has not been loaded (yet).
static inline int32_t file_dircount(const File *file)
{
	return file->dircount;
}

// Loads the number of files in a directory and saves it to `file`.
uint32_t path_dircount(const char *path);

// Loads the number of files in a directory and saves it to `file`.
static inline int32_t file_dircount_load(File *file)
{
	return path_dircount(file->path);
}

// Set `file->dircount` to `count`.
static inline void file_dircount_set(File *file, int32_t ct)
{
	file->dircount = ct;
}

// Returns the modification time.
static inline long file_mtime(const File *file)
{
	return file->lstat.st_mtime;
}

// Returns the creation time.
static inline long file_ctime(const File *file)
{
	return file->lstat.st_ctime;
}

// Returns `nlink` of `file`.
static inline long file_nlink(const File *file)
{
	return file->lstat.st_nlink;
}

// Returns the filesize in bytes.
static inline long file_size(const File *file)
{
	return file->stat.st_size;
}

// Writes a human readable representation of the filesize to buf. buf size of 8 should be fine.
static inline const char *file_size_readable(const File *file, char *buf)
{
	return readable_filesize(file_size(file), buf);
}

// Returns a string of the owners name.
const char* file_owner(const File *file);

// Returns a string of the groups name.
const char *file_group(const File *file);

// Returns a readable string of the permissions, e.g. `rwxr--r--`.
const char *file_perms(const File *file);

static inline bool file_hidden(const File *file)
{
	return file->hidden;
}

static inline int file_error(const File *file)
{
	return file->error;
}
