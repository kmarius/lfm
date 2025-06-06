#include "util.h"

#include "config.h"

#include "stc/cstr.h"

#include <magic.h>

#include <ctype.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h> // strcasecmp
#include <time.h>

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

char *readable_filesize(double size, char *buf) {
  int32_t i = 0;
  const char *units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
  while (size > 1024) {
    size /= 1024;
    i++;
  }
  sprintf(buf, "%.*f%s", i > 0 ? 1 : 0, size, units[i]);
  return buf;
}

uint64_t current_micros(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t)tv.tv_sec) * 1000 * 1000 + tv.tv_usec;
}

uint64_t current_millis(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

int mkdir_p(char *path, __mode_t mode) {
  char *sep = strrchr(path, '/');
  if (sep && sep != path) {
    *sep = 0;
    mkdir_p(path, mode);
    *sep = '/';
  }
  return mkdir(path, mode);
}

int make_dirs(zsview path, __mode_t mode) {
  static char buf[PATH_MAX + 1];
  if (zsview_is_empty(path)) {
    buf[0] = 0;
  } else {
    memcpy(buf, path.str, path.size + 1);
  }
  return mkdir_p(dirname(buf), mode);
}

// https://stackoverflow.com/questions/9152978/include-unix-utility-file-in-c-program
bool get_mimetype(const char *path, char *dest, size_t sz) {
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

int strcasecmp_strict(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    char c1 = *s1;
    char c2 = *s2;

    char lower1 = tolower((unsigned char)c1);
    char lower2 = tolower((unsigned char)c2);

    if (lower1 != lower2) {
      return (unsigned char)lower1 - (unsigned char)lower2;
    }

    if (c1 != c2) {
      if (isupper((unsigned char)c1) && islower((unsigned char)c2)) {
        return 1;
      } else if (islower((unsigned char)c1) && isupper((unsigned char)c2)) {
        return -1;
      }
    }

    s1++;
    s2++;
  }

  // End of one or both strings
  return (unsigned char)*s1 - (unsigned char)*s2;
}

int shorten_name(zsview name, char *buf, int max_len, bool has_ext) {
  size_t pos = 0;
  char *ptr = buf;
  if (max_len <= 0) {
    *ptr = 0;
    return 0;
  }

  int name_len = zsview_u8_size(name);
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
  int ext_len = zsview_u8_size(ext);

  int trunc_len = strlen(cfg.truncatechar);

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
