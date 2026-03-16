#include "util.h"

#include "config.h"
#include "defs.h"

#include "stc/cstr.h"

#include <magic.h>

#include <ctype.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h> // strcasecmp

#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

char *rtrim(char *s) {
  char *t = s;
  char *end = s - 1;
  while (*t) {
    if (!isspace(*t)) {
      end = t;
    }
    t++;
  }
  *++end = 0;
  return s;
}

char *ltrim(char *s) {
  s--;
  while (isspace(*++s)) {
  }
  return s;
}

char *strcasestr(const char *str, const char *sub) {

  if (*sub == 0) {
    return (char *)str;
  }

  for (; *str != 0; str++) {
    if (tolower(*str) != tolower(*sub)) {
      continue;
    }
    if (hascaseprefix(str, sub)) {
      return (char *)str;
    }
  }

  return NULL;
}

bool hascaseprefix(const char *restrict string, const char *restrict prefix) {
  while (*prefix != 0) {
    if (tolower(*prefix++) != tolower(*string++)) {
      return false;
    }
  }
  return true;
}

char *readable_filesize(f64 size, char *buf) {
  i32 i = 0;
  const char *units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
  while (size > 1024) {
    size /= 1024;
    i++;
  }
  sprintf(buf, "%.*f%s", i > 0 ? 1 : 0, size, units[i]);
  return buf;
}

u64 current_micros(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((u64)tv.tv_sec) * 1000 * 1000 + tv.tv_usec;
}

u64 current_millis(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((u64)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

i32 mkdir_p(char *path, __mode_t mode) {
  char *sep = strrchr(path, '/');
  if (sep && sep != path) {
    *sep = 0;
    mkdir_p(path, mode);
    *sep = '/';
  }
  return mkdir(path, mode);
}

i32 make_dirs(zsview path, __mode_t mode) {
  static char buf[PATH_MAX + 1];
  if (zsview_is_empty(path)) {
    buf[0] = 0;
  } else {
    memcpy(buf, path.str, path.size + 1);
  }
  return mkdir_p(dirname(buf), mode);
}

// https://stackoverflow.com/questions/9152978/include-unix-utility-file-in-c-program
bool get_mimetype(const char *path, char *dest, usize sz) {
  bool ret = true;
  magic_t magic = magic_open(MAGIC_MIME_TYPE);
  magic_load(magic, NULL);
  const char *mime = magic_file(magic, path);
  if (mime == 0 ||
      strncmp(mime, "cannot open", sizeof "cannot open" - 1) == 0) {
    ret = false;
    *dest = 0;
  } else {
    strncpy(dest, mime, sz);
  }
  magic_close(magic);
  return ret;
}

bool valgrind_active(void) {
  char *preload = getenv("LD_PRELOAD");
  if (!preload) {
    return false;
  }
  return (strstr(preload, "/valgrind/") || strstr(preload, "/vgpreload"));
}

i32 strcasecmp_strict(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    char c1 = *s1;
    char c2 = *s2;

    char lower1 = tolower((u8)c1);
    char lower2 = tolower((u8)c2);

    if (lower1 != lower2) {
      return (u8)lower1 - (u8)lower2;
    }

    if (c1 != c2) {
      if (isupper((u8)c1) && islower((u8)c2)) {
        return 1;
      } else if (islower((u8)c1) && isupper((u8)c2)) {
        return -1;
      }
    }

    s1++;
    s2++;
  }

  // End of one or both strings
  return (u8)*s1 - (u8)*s2;
}

i32 shorten_name(zsview name, char *buf, i32 max_len, bool has_ext) {
  usize pos = 0;
  char *ptr = buf;
  if (max_len <= 0) {
    *ptr = 0;
    return 0;
  }

  i32 name_len = zsview_u8_size(name);
  if (name_len <= max_len) {
    // everything fits
    memcpy(buf, name.str, name.size + 1);
    return name_len;
  }

  zsview ext = zsview_tail(name, 0);
  if (has_ext) {
    const char *ptr = strrchr(name.str, '.');
    if (ptr != NULL && ptr != name.str) {
      ext = zsview_tail(name, name.size - (ptr - name.str));
    }
  }
  i32 ext_len = zsview_u8_size(ext);

  i32 trunc_len = strlen(cfg.truncatechar);

  if (max_len > ext_len + 1) {
    // print extension and as much of the name as possible
    csview cs = zsview_u8_subview(name, 0, max_len - ext_len - 1);

    memcpy(buf + pos, cs.buf, cs.size);
    pos += cs.size;

    memcpy(buf + pos, cfg.truncatechar, trunc_len);
    pos += trunc_len;

    memcpy(buf + pos, ext.str, ext.size + 1);
  } else if (max_len >= 5) {
    // print first char of the name and as mutch of the extension as possible
    csview cs = zsview_u8_subview(name, 0, 1);

    memcpy(buf + pos, cs.buf, cs.size);
    pos += cs.size;

    memcpy(buf + pos, cfg.truncatechar, trunc_len);
    pos += trunc_len;

    cs = zsview_u8_subview(ext, 0, max_len - 2 - 1);

    memcpy(buf + pos, cs.buf, cs.size);
    pos += cs.size;

    memcpy(buf + pos, cfg.truncatechar, trunc_len + 1);
  } else if (max_len > 1) {
    csview cs = zsview_u8_subview(name, 0, max_len - 1);
    memcpy(buf + pos, cs.buf, cs.size);
    pos += cs.size;

    memcpy(buf + pos, cfg.truncatechar, trunc_len + 1);
  } else {
    // try one char?
    csview cs = zsview_u8_subview(name, 0, 1);

    memcpy(buf + pos, cs.buf, cs.size);
    pos += cs.size;
    buf[pos] = 0;
  }

  return max_len;
}
