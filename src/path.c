#include "path.h"

#include "memory.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <assert.h>

#include <libgen.h>
#include <linux/limits.h>
#include <string.h>

bool path_isroot(zsview path) {
  return !zsview_is_empty(path) && path.str[0] == '/' && path.str[1] == 0;
}

zsview path_parent_s(zsview path) {
  if (zsview_is_empty(path) || path_isroot(path)) {
    return c_zv("");
  }

  static char tmp[PATH_MAX + 1];
  memcpy(tmp, path.str, path.size + 1);
  return zsview_from(dirname(tmp));
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

cstr path_replace_tilde(zsview path) {
  if (zsview_is_empty(path) || path.str[0] != '~' || path.str[1] != '/') {
    return cstr_from_zv(path);
  }

  zsview home = getenv_zv("HOME");
  cstr res = cstr_with_capacity(path.size + home.size);
  path.str++; // skip ~
  cstr_append_zv(&res, home);
  cstr_append_zv(&res, path);
  return res;
}

// This works in-place and only decreases the length.
// Starting position is unchanged.
// replace //
// replace /./
// replace /../ and kill one component to the left
static char *remove_slashes_and_dots(char *const path, size_t len_in,
                                     size_t *len_out) {
  char *p = path; // read position
  char *q = path; // write position
  const char *end = path + len_in;
  while (p < end) {
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
      } while (p < end && *p != '/');
    }
  }
  if (q == path) {
    q++;
  } else if (q > path + 1 && *(q - 1) == '/') {
    q--;
  }
  *q = 0;

  if (len_out)
    *len_out = q - path;

  return path;
}

// buf is required to be of size PATH_MAX + 1
char *path_normalize(const char *path, const char *pwd, char *buf,
                     size_t len_in, size_t *len_out) {
  if (path[0] == '~' && (path[1] == 0 || path[1] == '/')) {
    // this increases the length

    const char *home = getenv("HOME");
    int len = strlen(home);
    if (len + len_in - 1 > PATH_MAX) {
      // too long, abort
      return NULL;
    }
    memcpy(buf, home, len);
    memcpy(buf + len, path + 1, len_in - 1);
    len_in += len - 1;
    buf[len_in] = 0;
  } else if (path[0] != '/') {
    // this increases the length

    if (!pwd) {
      pwd = getenv("PWD");
    }
    int len = strlen(pwd);
    if (len + len_in + 1 > PATH_MAX) {
      // too long, abort
      return NULL;
    }

    memcpy(buf, pwd, len);
    buf[len] = '/';
    memcpy(buf + len + 1, path, len_in);
    len_in += len + 1;
    buf[len_in] = 0;
  } else {
    // absolute path

    if (len_in + 1 > PATH_MAX) {
      // too long, abort
      return NULL;
    }

    memcpy(buf, path, len_in);
    buf[len_in] = 0;
  }
  remove_slashes_and_dots(buf, len_in, len_out);
  return buf;
}

// could be done without strncopying first
char *path_normalize_a(const char *path, const char *pwd, size_t len_in,
                       size_t *len_out) {
  char buf[PATH_MAX + 1];
  size_t len;
  if (path_normalize(path, pwd, buf, len_in, &len) == NULL) {
    // input too long
    return NULL;
  }
  if (len_out)
    *len_out = len;
  return strndup(buf, len);
}
