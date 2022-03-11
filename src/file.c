#include <bits/types.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <linux/limits.h> // PATH_MAX
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // readlink

#include "file.h"
#include "util.h" // asprintf

#define T File

#define FILE_INITIALIZER ((T) { \
		.dircount = -1,\
		})

T *file_init(T *t, const char *dir, const char *name)
{
	char buf[PATH_MAX] = {0};

	*t = FILE_INITIALIZER;

	bool isroot = dir[1] == 0 && dir[0] == '/';
	asprintf(&t->path, "%s/%s", isroot ? "" : dir, name);

	t->name = strrchr(t->path, '/') + 1;
	t->hidden = *t->name == '.';
	t->ext = strrchr(t->name, '.');
	if (t->ext == t->name)
		t->ext = NULL;

	if (lstat(t->path, &t->lstat) == -1) {
		if (errno == ENOENT) {
			free(t->path);
			free(t);
			return NULL;
		} else {
			t->error = errno;
			return t;
		}
	}

	if (S_ISLNK(t->lstat.st_mode)) {
		if (stat(t->path, &t->stat) == -1) {
			t->isbroken = true;
			t->stat = t->lstat;
		}
		if (readlink(t->path, buf, sizeof(buf)) == -1)
			t->isbroken = true;
		else
			t->link_target = strdup(buf);
	} else {
		// for non-symlinks stat == lstat
		t->stat = t->lstat;
	}

	if (file_isdir(t))
		t->ext = NULL;
	t->isexec = t->stat.st_mode & (1 | 8 | 64);

	return t;
}


T *file_create(const char *dir, const char *name)
{
	return file_init(malloc(sizeof(T)), dir, name);
}


void file_deinit(T *t)
{
	if (!t)
		return;

	free(t->path);
	free(t->link_target);
}


void file_destroy(T *t)
{
	file_deinit(t);
	free(t);
}


uint32_t path_dircount(const char *path)
{
	struct dirent *dp;

	DIR *dirp = opendir(path);
	if (!dirp)
		return 0;

	uint32_t ct;
	for (ct = 0; (dp = readdir(dirp)); ct++) ;
	closedir(dirp);
	return ct - 2;
}


static char filetypeletter(int mode)
{
	char c;

	if (S_ISREG(mode))
		c = '-';
	else if (S_ISDIR(mode))
		c = 'd';
	else if (S_ISBLK(mode))
		c = 'b';
	else if (S_ISCHR(mode))
		c = 'c';
#ifdef S_ISFIFO
	else if (S_ISFIFO(mode))
		c = 'p';
#endif /* S_ISFIFO */
#ifdef S_ISLNK
	else if (S_ISLNK(mode))
		c = 'l';
#endif /* S_ISLNK */
#ifdef S_ISSOCK
	else if (S_ISSOCK(mode))
		c = 's';
#endif /* S_ISSOCK */
#ifdef S_ISDOOR
	/* Solaris 2.6, etc. */
	else if (S_ISDOOR(mode))
		c = 'D';
#endif /* S_ISDOOR */
	else {
		/* Unknown type -- possibly a regular file? */
		c = '?';
	}
	return c;
}


const char *file_perms(const T *t)
{
	static const char *rwx[] = {
		"---", "--x", "-w-", "-wx",
		"r--", "r-x", "rw-", "rwx"};
	static char bits[11];

	const int mode = t->stat.st_mode;
	bits[0] = filetypeletter(mode);
	strcpy(&bits[1], rwx[(mode >> 6) & 7]);
	strcpy(&bits[4], rwx[(mode >> 3) & 7]);
	strcpy(&bits[7], rwx[(mode & 7)]);
	if (mode & S_ISUID)
		bits[3] = (mode & S_IXUSR) ? 's' : 'S';
	if (mode & S_ISGID)
		bits[6] = (mode & S_IXGRP) ? 's' : 'l';
	if (mode & S_ISVTX)
		bits[9] = (mode & S_IXOTH) ? 't' : 'T';
	bits[10] = '\0';
	return bits;
}


const char* file_owner(const T *t)
{
	static char owner[32];
	static uid_t cached_uid = UINT_MAX;
	struct passwd *pwd;

	if (t->lstat.st_uid != cached_uid) {
		if ((pwd = getpwuid(t->lstat.st_uid))) {
			strncpy(owner, pwd->pw_name, sizeof(owner)-1);
			owner[sizeof(owner)-1] = 0;
			cached_uid = t->lstat.st_uid;
		} else {
			owner[0] = 0;
			cached_uid = INT_MAX;
		}
	}
	return owner;
}


// getgrgid() is somewhat slow, so we cache one call
const char *file_group(const T *t)
{
	static char group[32];
	static gid_t cached_gid = UINT_MAX;
	struct group *grp;

	if (t->lstat.st_gid != cached_gid) {
		if ((grp = getgrgid(t->lstat.st_gid))) {
			strncpy(group, grp->gr_name, sizeof(group)-1);
			group[sizeof(group)-1] = 0;
			cached_gid = t->lstat.st_gid;
		} else {
			group[0] = 0;
			cached_gid = INT_MAX;
		}
	}
	return group;
}
