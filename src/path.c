#include "path.h"

#include "memory.h"

#include <assert.h>

#include <libgen.h>
#include <linux/limits.h>

bool path_isroot(const char *path) {
  return *path == '/' && *(path + 1) == 0;
}

char *path_parent_s(const char *path) {
  if (path_isroot(path)) {
    return NULL;
  }

  static char tmp[PATH_MAX + 1];
  strncpy(tmp, path, sizeof tmp - 1);
  return dirname(tmp);
}

char *realpath_s(const char *p) {
  static char fullpath[PATH_MAX + 1];
  return realpath(p, fullpath);
}

char *basename_s(const char *p) {
  static char buf[PATH_MAX + 1];
  strncpy(buf, p, sizeof buf - 1);
  return basename(buf);
}

char *dirname_s(const char *p) {
  static char buf[PATH_MAX + 1];
  strncpy(buf, p, sizeof buf - 1);
  return dirname(buf);
}

char *path_replace_tilde(const char *path) {
  if (path[0] != '~' || path[1] != '/') {
    return strdup(path);
  }

  const char *home = getenv("HOME");
  const int l1 = strlen(path);
  const int l2 = strlen(home);
  char *ret = xmalloc(l1 - 1 + l2 + 1);
  strcpy(ret, home);
  strcpy(ret + l2, path + 1);
  return ret;
}

// replace //
// replace /./
// replace /../ and kill one component to the left
static char *remove_slashes_and_dots(char *path) {
  char *p = path; // read position
  char *q = path; // write position
  while (*p) {
    assert(*p == '/');
    if (*(p + 1) == '/') {
      p++;
    } else if (*(p + 1) == '.' && (*(p + 2) == '/' || *(p + 2) == 0)) {
      p += 2;
    } else if (*(p + 1) == '.' && *(p + 2) == '.' &&
               (*(p + 3) == '/' || *(p + 3) == 0)) {
      p += 3;
      if (q > path) {
        q--;
        while (*q != '/') {
          q--;
        }
      }
    } else {
      do {
        *q++ = *p++;
      } while (*p && *p != '/');
    }
  }
  if (q == path) {
    q++;
  } else if (q > path + 1 && *(q - 1) == '/') {
    q--;
  }
  *q = 0;

  return path;
}

char *path_normalize(const char *path, const char *pwd, char *buf) {
  if (path[0] == '~') {
    const char *home = getenv("HOME");
    const int l1 = strlen(home);
    strcpy(buf, home);
    strcpy(buf + l1, path + 1);
  } else if (path[0] != '/') {
    if (!pwd) {
      pwd = getenv("PWD");
    }
    const int l1 = strlen(pwd);
    strcpy(buf, pwd);
    *(buf + l1) = '/';
    strcpy(buf + l1 + 1, path);
  } else {
    strcpy(buf, path);
  }
  remove_slashes_and_dots(buf);
  return buf;
}

// could be done without strncopying first
char *path_normalize_a(const char *path, const char *pwd) {
  char *p;
  // replace ~ or prepend PWD
  if (path[0] == '~') {
    const char *home = getenv("HOME");
    const int l1 = strlen(home);
    const int l2 = strlen(path);
    p = xmalloc(l2 - 1 + l1 + 1);
    strcpy(p, home);
    strcpy(p + l1, path + 1);
  } else if (path[0] != '/') {
    if (!pwd) {
      pwd = getenv("PWD");
    }
    const int l2 = strlen(path);
    const int l1 = strlen(pwd);
    p = xmalloc(l1 + l2 + 2);
    strcpy(p, pwd);
    *(p + l1) = '/';
    strcpy(p + l1 + 1, path);
  } else {
    p = strdup(path);
  }
  remove_slashes_and_dots(p);
  return p;
}
