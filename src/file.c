#include "file.h"

#include "memory.h"
#include "stc/cstr.h"
#include "stc/zsview.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <bits/types.h>
#include <dirent.h>
#include <grp.h>
#include <linux/limits.h> // PATH_MAX
#include <pwd.h>
#include <unistd.h> // readlink

static inline zsview name_from_path(cstr *path) {
  const char *last_slash = strrchr(cstr_str(path), '/') + 1;
  int pos = last_slash - cstr_str(path);
  return zsview_from_pos(cstr_zv(path), pos);
}

static inline zsview ext_from_name(zsview *name) {
  const char *last_dot = strrchr(name->str, '.');
  if (last_dot) {
    int pos = last_dot - name->str;
    if (pos > 0) {
      return zsview_from_pos(*name, pos);
    }
  }
  // no extension
  return c_zv("");
}

File *file_create(const char *dir, const char *name, bool load_info) {
  char buf[PATH_MAX + 1] = {0};

  File *f = xcalloc(1, sizeof *f);
  memset(f, 0, sizeof *f);

  f->dircount = -1;

  const bool isroot = dir[1] == 0 && dir[0] == '/';
  // neet to build stc to use cstr_from_fmt
  int len = snprintf(buf, sizeof buf - 1, "%s/%s", isroot ? "" : dir, name);
  f->path = cstr_with_n(buf, len);
  f->name = name_from_path(&f->path);
  f->ext = ext_from_name(&f->name);
  f->hidden = *file_name_str(f) == '.';

  if (lstat(file_path(f), &f->lstat) == -1) {
    if (errno == ENOENT) {
      cstr_drop(&f->path);
      xfree(f);
      return NULL;
    } else {
      f->error = errno;
      return f;
    }
  }

  if (S_ISLNK(f->lstat.st_mode)) {
    if (load_info) {
      if (stat(file_path(f), &f->stat) == -1) {
        f->isbroken = true;
        f->stat = f->lstat;
      }
    }
    ssize_t len = readlink(file_path(f), buf, sizeof buf);
    if (len == -1) {
      f->isbroken = true;
    } else {
      f->link_target = cstr_with_n(buf, len);
    }
  } else {
    // for non-symlinks stat == lstat
    f->stat = f->lstat;
  }

  if (file_isdir(f)) {
    f->ext = c_zv("");
    if (load_info) {
      f->dircount = path_dircount(file_path(f));
    }
  }

  return f;
}

void file_destroy(File *f) {
  if (!f) {
    return;
  }

  cstr_drop(&f->path);
  cstr_drop(&f->link_target);
  xfree(f);
}

uint32_t path_dircount(const char *path) {
  struct dirent *dp;

  DIR *dirp = opendir(path);
  if (!dirp) {
    return 0;
  }

  uint32_t ct;
  for (ct = 0; (dp = readdir(dirp)); ct++) {
  }
  closedir(dirp);
  return ct - 2;
}

static char filetypeletter(int mode) {
  char c;

  if (S_ISREG(mode)) {
    c = '-';
  } else if (S_ISDIR(mode)) {
    c = 'd';
  } else if (S_ISBLK(mode)) {
    c = 'b';
  } else if (S_ISCHR(mode)) {
    c = 'c';
  }
#ifdef S_ISFIFO
  else if (S_ISFIFO(mode)) {
    c = 'p';
  }
#endif /* S_ISFIFO */
#ifdef S_ISLNK
  else if (S_ISLNK(mode)) {
    c = 'l';
  }
#endif /* S_ISLNK */
#ifdef S_ISSOCK
  else if (S_ISSOCK(mode)) {
    c = 's';
  }
#endif /* S_ISSOCK */
#ifdef S_ISDOOR
  /* Solaris 2.6, etc. */
  else if (S_ISDOOR(mode)) {
    c = 'D';
  }
#endif /* S_ISDOOR */
  else {
    /* Unknown type -- possibly a regular file? */
    c = '?';
  }
  return c;
}

const char *file_perms(const File *f) {
  static const char *rwx[] = {"---", "--x", "-w-", "-wx",
                              "r--", "r-x", "rw-", "rwx"};
  static char bits[11];

  const int mode = f->stat.st_mode;
  bits[0] = filetypeletter(mode);
  strcpy(&bits[1], rwx[(mode >> 6) & 7]);
  strcpy(&bits[4], rwx[(mode >> 3) & 7]);
  strcpy(&bits[7], rwx[(mode & 7)]);
  if (mode & S_ISUID) {
    bits[3] = (mode & S_IXUSR) ? 's' : 'S';
  }
  if (mode & S_ISGID) {
    bits[6] = (mode & S_IXGRP) ? 's' : 'l';
  }
  if (mode & S_ISVTX) {
    bits[9] = (mode & S_IXOTH) ? 't' : 'T';
  }
  bits[10] = '\0';
  return bits;
}

const char *file_owner(const File *f) {
  static char name[32];
  static uid_t cached_uid = UINT_MAX;

  unsigned int uid = f->lstat.st_uid;
  if (uid != cached_uid) {
    struct passwd *pwd = getpwuid(uid);
    if (pwd) {
      strncpy(name, pwd->pw_name, sizeof name - 1);
    } else {
      snprintf(name, sizeof name, "%d/UNKNOWN", uid);
    }
    name[sizeof name - 1] = 0;
    cached_uid = f->lstat.st_uid;
  }
  return name;
}

// getgrgid() is somewhat slow, so we cache one call
const char *file_group(const File *f) {
  static char name[32];
  static gid_t cached_gid = UINT_MAX;

  unsigned int gid = f->lstat.st_gid;
  if (gid != cached_gid) {
    struct group *grp = getgrgid(gid);
    if (grp) {
      strncpy(name, grp->gr_name, sizeof name - 1);
    } else {
      snprintf(name, sizeof name, "%d/UNKNOWN", gid);
    }
    name[sizeof name - 1] = 0;
    cached_gid = gid;
  }
  return name;
}
