#include "util.h"

#include "memory.h"

#include <magic.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wctype.h>

#include <libgen.h>
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

const wchar_t *wstrcasestr(const wchar_t *str, const wchar_t *sub) {

  if (*sub == 0) {
    return str;
  }

  for (; *str != 0; str++) {
    if (towlower(*str) != towlower(*sub)) {
      continue;
    }
    if (haswcaseprefix(str, sub)) {
      return str;
    }
  }

  return NULL;
}

bool haswprefix(const wchar_t *restrict string,
                const wchar_t *restrict prefix) {
  while (*prefix != 0) {
    if (*prefix++ != *string++) {
      return false;
    }
  }
  return true;
}

bool haswcaseprefix(const wchar_t *restrict string,
                    const wchar_t *restrict prefix) {
  while (*prefix != 0) {
    if (towlower(*prefix++) != towlower(*string++)) {
      return false;
    }
  }
  return true;
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

bool hasprefix(const char *restrict string, const char *restrict prefix) {
  while (*prefix != 0) {
    if (*prefix++ != *string++) {
      return false;
    }
  }
  return true;
}

bool hassuffix(const char *suf, const char *str) {
  const char *s = strrchr(str, suf[0]);
  return s && strcasecmp(s, suf) == 0;
}

const char *strcaserchr(const char *str, char c) {
  const char *last = NULL;
  for (; *str != 0; str++) {
    if (*str == c) {
      last = str;
    }
  }
  return last;
}

bool hascasesuffix(const char *suf, const char *str) {
  const char *s = strcaserchr(str, suf[0]);
  return s && strcasecmp(s, suf) == 0;
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

// https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
int msleep(uint32_t msec) {
  struct timespec ts;
  int res;

  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;

  do {
    res = nanosleep(&ts, &ts);
  } while (res != 0 && errno == EINTR);

  return res;
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

int vasprintf(char **dst, const char *format, va_list args) {
  va_list args_copy;
  va_copy(args_copy, args);
  *dst = xmalloc(vsnprintf(NULL, 0, format, args) + 1);
  int ret = vsprintf(*dst, format, args_copy);
  va_end(args_copy);
  return ret;
}

int asprintf(char **dst, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int ret = vasprintf(dst, format, args);
  va_end(args);
  return ret;
}

wchar_t *ambstowcs(const char *s, int *len) {
  const int l = mbstowcs(NULL, s, 0);
  wchar_t *ws = xmalloc((l + 1) * sizeof *ws);
  mbstowcs(ws, s, l + 1);
  if (len) {
    *len = l;
  }
  return ws;
}

// https://stackoverflow.com/questions/9152978/include-unix-utility-file-in-c-program
bool get_mimetype(const char *path, char *dest, size_t sz) {
  bool ret = true;
  magic_t magic = magic_open(MAGIC_MIME_TYPE);
  magic_load(magic, NULL);
  const char *mime = magic_file(magic, path);
  if (!mime || hasprefix(mime, "cannot open")) {
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
