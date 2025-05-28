#include "infoline.h"

#include "config.h"
#include "lfm.h"
#include "log.h"
#include "macros.h"
#include "memory.h"
#include "ncutil.h"
#include "spinner.h"
#include "ui.h"
#include "util.h"

#include <curses.h>

#include <linux/limits.h>
#include <notcurses/notcurses.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

// some placeholders don't change (user, host) and are not counted here
#define PLACEHOLDERS_MAX 16

// holds any static text including ansi sequences etc.
#define STATIC_BUF_SZ 1024

// static data we only set once
static int uid = -1;
static char user[64 + 1] = {0};
static char host[HOST_NAME_MAX + 1] = {0};
static char home[64 + 1] = {0};
static int home_len = 0; // printed length of home

// this buffer holds any static text including ansi sequences etc.
static char static_buf[STATIC_BUF_SZ];
static char *static_buf_ptr;
// this includes size of elements not in the static buf, e.g. 1 for the spinner
static int static_len = 0;

static int num_placeholders = 0;
static struct {
  char *next; // points into buf, to print whatever follows this placeholder
  char c;     // placeholder type
  int16_t next_len;        // printed length of whatever follows
  int16_t replacement_len; // printed length of the replaced placeholder, if
                           // known, or 0
  void *ptr; // data belonging to the placeholder, possibly a char* or wchar_t*
} placeholders[PLACEHOLDERS_MAX] = {0};

// index positions of some placeholders
static struct {
  int file, path, spinner, spacer, mode;
} idx;

static inline void draw_custom(Ui *ui);
static inline void draw_default(Ui *ui);
static inline int shorten_file_name(wchar_t *name, int name_len, int max_len,
                                    bool has_ext);
static inline int shorten_path(wchar_t *path, int path_len, int max_len);

static inline bool should_draw_default() {
  return static_len == 0;
}

void infoline_init(Ui *ui) {
  (void)ui;
  gethostname(host, sizeof host);
  uid = getuid();
  strncpy(user, getenv("USER"), sizeof user - 1);
  if (uid == 0) {
    // not a prefix of any absolute path, so never replacing /root with ~
    home_len = sprintf(home, "-");
  } else {
    const char *env_home = getenv("HOME");
    if (env_home) {
      strncpy(home, env_home, sizeof home - 1);
      home[sizeof home - 1] = 0;
    }
    home_len = mbstowcs(NULL, home, 0);
  }
}

void infoline_parse(zsview infoline) {
  log_trace("parsing size=%u", infoline.size);

  memset(&idx, 0, sizeof idx);
  memset(placeholders, 0, sizeof placeholders);
  static_buf_ptr = static_buf;
  num_placeholders = 0;
  placeholders[num_placeholders].next = static_buf_ptr;
  placeholders[num_placeholders].c = 0;
  num_placeholders++;
  static_len = 0;

  if (zsview_is_empty(infoline)) {
    // this will draw the default line
    return;
  }

  char const *buf_end = static_buf + sizeof static_buf - 1;
  const char *line_end = infoline.str + infoline.size;
  for (const char *ptr = infoline.str;
       ptr < line_end && static_buf_ptr < buf_end; ptr++) {
    if (*ptr != '%') {
      *static_buf_ptr++ = *ptr;
    } else {
      if (num_placeholders == PLACEHOLDERS_MAX) {
        log_error("too many placeholders");
        break;
      }
      ptr++;
      switch (*ptr) {
      case 0:
        // malformed
        break;
      case 'u':
        static_buf_ptr += snprintf(
            static_buf_ptr,
            sizeof static_buf - 1 - (static_buf_ptr - static_buf), "%s", user);
        break;
      case 'h':
        static_buf_ptr += snprintf(
            static_buf_ptr,
            sizeof static_buf - 1 - (static_buf_ptr - static_buf), "%s", host);
        break;
      case 'p':
        if (idx.path != 0) {
          log_info("ignoring duplicate path placeholder");
          continue;
        }
        placeholders[num_placeholders].next = static_buf_ptr + 1;
        placeholders[num_placeholders].c = 'p';
        idx.path = num_placeholders;
        num_placeholders++;
        *static_buf_ptr++ = 0;
        break;
      case 'f':
        if (idx.file != 0) {
          log_info("ignoring duplicate file placeholder");
          continue;
        }
        placeholders[num_placeholders].next = static_buf_ptr + 1;
        placeholders[num_placeholders].c = 'f';
        idx.file = num_placeholders;
        num_placeholders++;
        *static_buf_ptr++ = 0;
        break;
      case 's':
        if (idx.spacer != 0) {
          log_info("ignoring duplicate spacer");
          continue;
        }
        placeholders[num_placeholders].next = static_buf_ptr + 1;
        placeholders[num_placeholders].c = 's';
        idx.spacer = num_placeholders;
        num_placeholders++;
        *static_buf_ptr++ = 0;
        break;
      case 'S':
        if (idx.spinner != 0) {
          log_info("ignoring duplicate spinner placeholder");
          continue;
        }
        placeholders[num_placeholders].next = static_buf_ptr + 1;
        placeholders[num_placeholders].c = 'S';
        placeholders[num_placeholders].replacement_len = 1;
        idx.spinner = num_placeholders;
        num_placeholders++;
        *static_buf_ptr++ = 0;
        break;
      case 'M':
        if (idx.mode != 0) {
          log_info("ignoring duplicate mode placeholder");
          continue;
        }
        placeholders[num_placeholders].next = static_buf_ptr + 1;
        placeholders[num_placeholders].c = 'M';
        idx.mode = num_placeholders;
        num_placeholders++;
        *static_buf_ptr++ = 0;
        break;
      case '%':
        *static_buf_ptr++ = '%';
        break;
      default:
        *static_buf_ptr++ = '%';
        *static_buf_ptr++ = *ptr;
      }
    }
  }

  *static_buf_ptr = 0;

  // length of all static tokens
  for (int i = 0; i < num_placeholders; i++) {
    size_t len = ansi_mblen(placeholders[i].next);
    placeholders[i].next_len = len;
    static_len += len;
    static_len += placeholders[i].replacement_len;
  }
}

void infoline_draw(Ui *ui) {
  struct ncplane *n = ui->planes.info;
  ncplane_erase(n);

  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  if (should_draw_default()) {
    draw_default(ui);
  } else {
    draw_custom(ui);
  }
}

static inline void draw_custom(Ui *ui) {
  struct ncplane *n = ui->planes.info;

  // longer file names/paths are truncated safely
  wchar_t file_buf[128] = {0};
  int file_len = 0;
  bool file_is_dir = false;
  const int file_buf_len = sizeof file_buf / sizeof file_buf[0];

  wchar_t path_buf[PATH_MAX] = {0};
  const int path_buf_len = sizeof path_buf / sizeof path_buf[0];

  // start of the string we are printing, points into path_buf
  wchar_t *path = path_buf;

  // we need to fit file/path placeholders into this
  // make sure to deduct any other dynamic placeholders from this
  int remaining = ui->x - static_len;

  if (idx.mode) {
    zsview mode = cstr_zv(&to_lfm(ui)->current_mode->name);
    placeholders[idx.mode].replacement_len = mode.size;
    placeholders[idx.mode].ptr = (void *)mode.str;
    remaining -= mode.size;
  }

  if (idx.file != 0) {
    file_buf[0] = 0;
    File *file = fm_current_file(&to_lfm(ui)->fm);
    if (file) {
      file_len = mbstowcs(file_buf, file_name_str(file), file_buf_len - 1);
      file_is_dir = file_isdir(file);
    }
  }

  if (idx.path != 0) {
    const Dir *dir = fm_current_dir(&to_lfm(ui)->fm);
    const wchar_t *path_buf_end = path_buf + path_buf_len;

    mbstowcs(path_buf, dir_path_str(dir), path_buf_len - 1);

    int path_remaining = remaining - file_len;

    // replace $HOME with ~
    if (cstr_starts_with(dir_path(dir), home)) {
      path += mbslen(home) - 1;
      // make sure we don't jump past the end of the buffer
      if (path < path_buf_end - 1) {
        *path = '~';
        path_remaining--;
      } else {
        // abort!
        path_remaining = 0;
        path = (wchar_t *)path_buf_end - 1;
      }
    }

    if (!dir_isroot(dir) && path_remaining > 0) {
      path_remaining--; // extra trailing '/', later
    }

    int len = wcslen(path);
    if (path_remaining < len) {
      wchar_t *path_begin = path;
      if (path[0] == '~') {
        path_begin++;
        len--;
      }
      shorten_path(path_begin, len, path_remaining);
      len = wcslen(path);
    }
    assert(path + len < path_buf_end);

    // trailing slash
    if (!dir_isroot(dir)) {
      if (path + len < path_buf_end - 1) {
        path[len] = '/';
        path[len + 1] = 0;
        len++;
      }
    }

    placeholders[idx.path].replacement_len = len;
    placeholders[idx.path].ptr = path;
    remaining -= len;

    assert(path < path_buf_end);
  }

  if (idx.file != 0) {
    if (remaining < file_len) {
      shorten_file_name(file_buf, file_len, remaining, !file_is_dir);
    }
    // TODO: could be returned by shorten_file_name
    placeholders[idx.file].replacement_len = wcslen(file_buf);
    placeholders[idx.file].ptr = file_buf;
  }

  for (int i = 0; i < num_placeholders; i++) {
    switch (placeholders[i].c) {
    case 0:
      break;
    case 'f':
      ncplane_putwstr(n, placeholders[i].ptr);
      break;
    case 'p':
      ncplane_putwstr(n, placeholders[i].ptr);
      break;
    case 's': {
      unsigned int x;
      ncplane_cursor_yx(n, NULL, &x);
      int remaining = ui->x - x;
      int l = 0;
      for (int j = i; j < num_placeholders; j++) {
        l += placeholders[j].next_len;
        l += placeholders[j].replacement_len;
      }
      if (remaining >= l) {
        ncplane_cursor_move_yx(n, 0, ui->x - l);
      }
    } break;
    case 'M':
      ncplane_putstr(n, placeholders[i].ptr);
      break;
    case 'S': {
      // store style/colors and initialize the spinner
      unsigned int x;
      ncplane_cursor_yx(n, NULL, &x);
      uint64_t channels = ncplane_channels(n);
      uint16_t style = ncplane_styles(n);

      spinner_on(&ui->spinner, 0, x, channels, style);
      // draw the current char immediately
      spinner_draw_char(&ui->spinner);
    } break;
    }
    ncplane_put_str_ansi(n, placeholders[i].next);
  }

  if (idx.spinner == 0) {
    spinner_off(&ui->spinner);
  }
}

static inline void draw_default(Ui *ui) {
  struct ncplane *n = ui->planes.info;

  ncplane_set_styles(n, NCSTYLE_BOLD);
  if (uid == 0) {
    ncplane_set_fg_palindex(n, COLOR_RED);
  } else {
    ncplane_set_fg_palindex(n, COLOR_GREEN);
  }
  ncplane_putstr_yx(n, 0, 0, user);
  ncplane_putchar(n, '@');
  ncplane_putstr(n, host);
  ncplane_set_fg_default(n);

  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_putchar(n, ':');
  ncplane_set_styles(n, NCSTYLE_BOLD);

  const Dir *dir = fm_current_dir(&to_lfm(ui)->fm);
  const File *file = dir_current_file(dir);
  int path_len = 0;
  int name_len = 0;
  wchar_t *path_ = ambstowcs(dir_path_str(dir), &path_len);
  wchar_t *path = path_;
  wchar_t *name = NULL;

  // shortening should work fine with ascii only names
  wchar_t *end = path + wcslen(path);
  unsigned int remaining;
  ncplane_cursor_yx(n, NULL, &remaining);
  remaining = ui->x - remaining;
  if (file) {
    name = ambstowcs(file_name_str(file), &name_len);
    remaining -= name_len;
  }
  ncplane_set_fg_palindex(n, COLOR_BLUE);
  if (cstr_starts_with(dir_path(dir), home)) {
    ncplane_putchar(n, '~');
    remaining--;
    path += home_len;
  }

  if (!dir_isroot(dir)) {
    remaining--; // printing another '/' later
  }

  /* TODO: check remaining < 0 here (on 2022-10-29) */

  // shorten path components if necessary {}
  while (*path && end - path > remaining) {
    ncplane_putchar(n, '/');
    remaining--;
    wchar_t *next = wcschr(++path, '/');
    if (!next) {
      next = end;
    }

    if (end - next <= remaining) {
      // Everything after the next component fits, we can print some of this one
      const int m = remaining - (end - next) - 1;
      if (m >= 2) {
        wchar_t *print_end = path + m;
        remaining -= m;
        while (path < print_end) {
          ncplane_putwc(n, *(path++));
        }

        if (*path != '/') {
          ncplane_putwc(n, cfg.truncatechar);
          remaining--;
        }
      } else {
        ncplane_putwc(n, *path);
        remaining--;
        path = next;
      }
      path = next;
    } else {
      // print one char only.
      ncplane_putwc(n, *path);
      remaining--;
      path = next;
    }
  }
  ncplane_putwstr(n, path);

  if (!dir_isroot(dir)) {
    ncplane_putchar(n, '/');
  }

  if (file) {
    ncplane_cursor_yx(n, NULL, &remaining);
    remaining = ui->x - remaining;
    ncplane_set_fg_default(n);
    print_shortened_w(n, name, name_len, remaining, !file_isdir(file));
  }

  xfree(path_);
  xfree(name);
}

/* TODO: make the following two functions return the length of the output
 * (and make the callers use it) (on 2022-10-29) */
/* TODO: use these in the default infoline drawer (on 2022-10-29) */

// max_len is not a strict upper bound, but we try to make path as short as
// possible. path probably shouldn't end with /
static inline int shorten_path(wchar_t *path, int path_len, int max_len) {
  wchar_t *ptr = path;
  wchar_t *end = path + path_len;
  if (path_len <= max_len) {
    return path_len;
  } else if (max_len <= 0) {
    // as short as possible
    while (*path) {
      *ptr++ = '/';
      wchar_t *next = wcschr(++path, '/');
      *ptr++ = *path;
      if (!next) {
        break;
      }
      path = next;
    }
  } else {
    while (*path && end - path > max_len) {
      *ptr++ = '/';
      max_len--;

      wchar_t *next = wcschr(++path, '/');
      if (!next) {
        next = end;
      }

      if (end - next <= max_len) {
        // Everything after the next component fits, we can print some of this
        // one
        const int m = max_len - (end - next) - 1;
        if (m >= 2) {
          const wchar_t *keep = path + m;
          max_len -= m;
          while (path < keep) {
            *ptr++ = *(path++);
          }

          if (*path != '/') {
            *ptr++ = cfg.truncatechar;
            max_len--;
          }
        } else {
          *ptr++ = *path;
          max_len--;
        }
      } else {
        // print one char only.
        *ptr++ = *path;
        max_len--;
      }

      path = next;
    }

    // any leftovers fit
    while (*path) {
      *ptr++ = *path++;
    }
  }

  *ptr = 0;

  return 0;
}

static inline int shorten_file_name(wchar_t *name, int name_len, int max_len,
                                    bool has_ext) {
  if (max_len <= 0) {
    *name = 0;
    return 0;
  }

  const wchar_t *ext = has_ext ? wcsrchr(name, L'.') : NULL;

  if (!ext || ext == name) {
    ext = name + name_len;
  }
  const int ext_len = name_len - (ext - name);

  wchar_t *ptr = name;

  int x = max_len;
  if (name_len <= max_len) {
    // everything fits
    return name_len;
  } else if (max_len > ext_len + 1) {
    // keep extension and as much of the name as possible
    int print_name_ind = max_len - ext_len - 1;
    ptr = name + print_name_ind;
    *ptr++ = cfg.truncatechar;
    while (*ext) {
      *ptr++ = *ext++;
    }
  } else if (max_len >= 5) {
    // keep first char of the name and as mutch of the extension as possible
    const wchar_t *keep = ext + max_len - 2 - 1;
    ptr++;
    *ptr++ = cfg.truncatechar;
    while (ext < keep && *ext) {
      *ptr++ = *ext++;
    }
    *ptr++ = cfg.truncatechar;
  } else if (max_len > 1) {
    wchar_t *name_end = name + max_len - 1;
    ptr = name_end;
    *ptr++ = cfg.truncatechar;
  } else {
    // first char only
    ptr++;
  }
  *ptr = 0;

  return x;
}
