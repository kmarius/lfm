#include "path.h"

#include "getpwd.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <assert.h>

#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

zsview path_parent_zv(zsview path) {
  if (zsview_is_empty(path) || path_is_root_zv(path)) {
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

zsview basename_zv(zsview path) {
  if (zsview_is_empty(path)) {
    return c_zv(".");
  }

  char *base = strrchr(path.str, '/');

  if (base == NULL) {
    return path;
  }
  i32 len = base - path.str;

  return zsview_tail(path, path.size - len - 1);
}

void dirname_cstr(cstr *path) {
  if (cstr_is_empty(path)) {
    cstr_assign_n(path, ".", 1);
    return;
  }

  char *base = strrchr(cstr_str(path), '/');
  if (base == NULL) {
    cstr_assign_n(path, ".", 1);
    return;
  }
  usize len = base - cstr_str(path);
  if (len == 0) {
    // root keeps its trailing slash
    cstr_resize(path, 1, 0);
  } else {
    cstr_resize(path, len, 0);
  }
}

cstr path_replace_tilde(zsview path) {
  if (zsview_is_empty(path) || path.str[0] != '~' || path.str[1] != '/') {
    return cstr_from_zv(path);
  }

  zsview home = getenv_zv("HOME");
  cstr res = cstr_with_capacity(path.size + home.size);
  path.str++; // skip ~
  path.size--;
  cstr_append_zv(&res, home);
  cstr_append_zv(&res, path);
  return res;
}

// This works in-place and only decreases the length.
// Starting position is unchanged.
// replace //
// replace /./
// replace /../ and kill one component to the left
static inline char *path_clean(char *const path, usize len_in, isize *len_out) {
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

zsview path_normalize3(zsview path, const char *pwd, char *buf, usize bufsz) {
  isize len = 0;

  if (zsview_eq(&path, &c_zv("~"))) {
    zsview home = getenv_zv("HOME");
    if ((usize)home.size + path.size - 1 > bufsz) {
      // too long, abort
      return c_zv("");
    }
    memcpy(buf, home.str, home.size);
    len = home.size;
  } else if (zsview_starts_with(path, "~/")) {
    // this increases the length

    zsview home = getenv_zv("HOME");
    if ((usize)home.size + path.size - 1 > bufsz) {
      // too long, abort
      return c_zv("");
    }
    memcpy(buf, home.str, home.size);
    memcpy(buf + home.size, path.str + 1, path.size); // includes nul
    len = home.size + path.size - 1;
  } else if (!path_is_absolute_zv(path)) {
    // this increases the length

    if (!pwd) {
      len = getpwd_buf(buf, bufsz);
      if (len < 0 || len + path.size + 1 > (isize)bufsz) {
        // too long, abort
        return c_zv("");
      }
    } else {
      len = strlen(pwd);
      if ((usize)len + path.size + 1 > bufsz) {
        // too long, abort
        return c_zv("");
      }

      memcpy(buf, pwd, len);
    }

    buf[len] = '/';
    memcpy(buf + len + 1, path.str, path.size + 1); // includes nul
    len += path.size + 1;
  } else {
    // absolute path

    if ((usize)path.size + 1 > bufsz) {
      // too long, abort
      return c_zv("");
    }

    memcpy(buf, path.str, path.size + 1); // includes nul
    len = path.size;
  }
  path_clean(buf, len, &len);
  return zsview_from_n(buf, len);
}

isize path_make_absolute(zsview path, char *buf, usize bufsz) {
  isize len = getpwd_buf(buf, bufsz);
  if (len < 0 || len + 1 + path.size + 1 > (isize)bufsz) {
    // buffer too small
    return -1;
  }
  buf[len++] = '/';
  memcpy(buf + len, path.str, path.size + 1); // includes nul
  path = zsview_from_n(buf, len);
  return len + path.size;
}

zsview name_ext(const zsview *name) {
  const char *last_dot = strrchr(name->str, '.');
  if (last_dot) {
    i32 pos = last_dot - name->str;
    if (pos > 0) {
      return zsview_from_pos(*name, pos);
    }
  }
  // no extension
  return zsview_from_pos(*name, name->size);
}

isize path_concat(zsview dir, zsview name, char *buf, usize bufsz) {
  usize len = dir.size + name.size + 1;
  if (len + 1 > bufsz) {
    return -1;
  }
  memcpy(buf, dir.str, dir.size);
  buf[dir.size] = '/';
  memcpy(buf + dir.size + 1, name.str, name.size + 1); // includes nul
  return len;
}
