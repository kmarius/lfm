#include <curses.h>
#include <unistd.h>

#include "config.h"
#include "infoline.h"
#include "lfm.h"
#include "macros.h"
#include "memory.h"
#include "ncutil.h"
#include "ui.h"

static inline void draw_custom_info(Ui *ui, const char *user, const char *host,
                                    const char *home);
static inline int shorten_file_name(wchar_t *name, int name_len, int max_len,
                                    bool has_ext);
static inline int shorten_path(wchar_t *path, int path_len, int max_len);

void infoline_set(Ui *ui, const char *line) {
  xfree(ui->infoline);
  ui->infoline = line ? strdup(line) : NULL;
  ui_redraw(ui, REDRAW_INFO);
}

void infoline_draw(Ui *ui) {
  static int uid = -1;
  static char user[32] = {0};
  static char host[HOST_NAME_MAX + 1] = {0};
  static char *home;
  static uint32_t home_len;

  if (user[0] == 0) {
    strncpy(user, getenv("USER"), sizeof user - 1);
    gethostname(host, sizeof host);
    home = getenv("HOME");
    home_len = mbstowcs(NULL, home, 0);
    uid = getuid();
  }

  struct ncplane *n = ui->planes.info;

  ncplane_erase(n);

  if (ui->infoline) {
    draw_custom_info(ui, user, host, home);
    return;
  }

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
  int path_len, name_len;
  wchar_t *path_ = ambstowcs(dir->path, &path_len);
  wchar_t *path = path_;
  wchar_t *name = NULL;

  // shortening should work fine with ascii only names
  wchar_t *end = path + wcslen(path);
  unsigned int remaining;
  ncplane_cursor_yx(n, NULL, &remaining);
  remaining = ui->ncol - remaining;
  if (file) {
    name = ambstowcs(file_name(file), &name_len);
    remaining -= name_len;
  }
  ncplane_set_fg_palindex(n, COLOR_BLUE);
  if (home && hasprefix(dir->path, home)) {
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
    remaining = ui->ncol - remaining;
    ncplane_set_fg_default(n);
    print_shortened_w(n, name, name_len, remaining, !file_isdir(file));
  }

  xfree(path_);
  xfree(name);
}

// default: %u@%h:%p/%f
static inline void draw_custom_info(Ui *ui, const char *user, const char *host,
                                    const char *home) {
  char buf[1024];
  char *buf_ptr = buf;
  char const *buf_end = buf + sizeof buf - 1;

  // path will be truncated first, then file
  char *path_ptr = NULL;
  char *file_ptr = NULL;
  char *spacer_ptr = NULL;

  for (const char *ptr = ui->infoline; *ptr && buf_ptr < buf_end; ptr++) {
    if (*ptr != '%') {
      *buf_ptr++ = *ptr;
    } else {
      ptr++;
      switch (*ptr) {
      case 0:
        // malformed
        break;
      case 'u':
        buf_ptr +=
            snprintf(buf_ptr, sizeof(buf) - 1 - (buf_ptr - buf), "%s", user);
        break;
      case 'h':
        buf_ptr +=
            snprintf(buf_ptr, sizeof(buf) - 1 - (buf_ptr - buf), "%s", host);
        break;
      case 'p':
        path_ptr = buf_ptr;
        *buf_ptr++ = 0;
        break;
      case 'f':
        file_ptr = buf_ptr;
        *buf_ptr++ = 0;
        break;
      case 's':
        spacer_ptr = buf_ptr;
        *buf_ptr++ = 0;
        break;
      case 'M':
        buf_ptr += snprintf(buf_ptr, sizeof(buf) - 1 - (buf_ptr - buf), "%s",
                            to_lfm(ui)->current_mode->name);
        break;
      case '%':
        *buf_ptr++ = '%';
        break;
      default:
        *buf_ptr++ = '%';
        *buf_ptr++ = *ptr;
      }
    }
  }

  *buf_ptr = 0;

  // length of all static tokens
  int static_len = ansi_mblen(buf);

  if (path_ptr) {
    static_len += ansi_mblen(path_ptr + 1);
  }
  if (file_ptr) {
    static_len += ansi_mblen(file_ptr + 1);
  }
  if (spacer_ptr) {
    static_len += ansi_mblen(spacer_ptr + 1);
  }

  int remaining = ui->ncol - static_len;

  wchar_t *file = NULL;
  int file_len = 0;
  bool file_is_dir = false;
  if (file_ptr) {
    const File *f = fm_current_file(&to_lfm(ui)->fm);
    file = ambstowcs(f ? file_name(f) : "", &file_len);
    file_is_dir = f ? file_isdir(f) : false;
  }

  int path_len = 0;
  wchar_t *path_buf = NULL; // to xfree later
  wchar_t *path =
      NULL; // passed to drawing function, possibly points into path_buf

  if (path_ptr) {
    // prepare path string: replace HOME with ~ and shorten if necessary

    const Dir *dir = fm_current_dir(&to_lfm(ui)->fm);

    path_len = mbstowcs(NULL, dir->path, 0);
    path_buf = xmalloc((path_len + 2) *
                       sizeof *path_buf); // extra space for trailing '/'
    path_len = mbstowcs(path_buf, dir->path, path_len + 1);
    path = path_buf;

    wchar_t *path_buf_ptr = path;
    int path_remaining = remaining - file_len;

    if (hasprefix(dir->path, home)) {
      const int n = mbslen(home);
      path += n - 1;
      path_buf_ptr += n - 1;
      *path_buf_ptr++ = '~';
      path_remaining--;
    }

    if (!dir_isroot(dir)) {
      path_remaining--; // extra trailing '/'
    }

    const int l = wcslen(path_buf_ptr);
    if (path_remaining < l) {
      shorten_path(path_buf_ptr, l, path_remaining);
    }

    if (!dir_isroot(dir)) {
      path_buf_ptr += wcslen(path_buf_ptr);
      *path_buf_ptr++ = '/';
      *path_buf_ptr = 0;
    }
    path_len = path_buf_ptr - path;
    remaining -= path_buf_ptr - path;
  }

  if (file_ptr) {
    if (remaining < file_len) {
      shorten_file_name(file, file_len, remaining, !file_is_dir);
    }
  }

  struct ncplane *n = ui->planes.info;
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  ncplane_addastr(n, buf);
  if (path_ptr && file_ptr) {
    if (path_ptr < file_ptr) {
      ncplane_putwstr(n, path);
      ncplane_addastr(n, path_ptr + 1);

      ncplane_putwstr(n, file);
      ncplane_addastr(n, file_ptr + 1);
    } else {
      ncplane_putwstr(n, file);
      ncplane_addastr(n, file_ptr + 1);

      ncplane_putwstr(n, path);
      ncplane_addastr(n, path_ptr + 1);
    }
  } else if (path_ptr) {
    ncplane_putwstr(n, path);
    ncplane_addastr(n, path_ptr + 1);
  } else if (file_ptr) {
    ncplane_putwstr(n, file);
    ncplane_addastr(n, file_ptr + 1);
  }

  if (spacer_ptr) {
    unsigned int r;
    ncplane_cursor_yx(n, NULL, &r);
    r = ui->ncol - r;
    while (ncplane_putchar(n, ' ') > 0)
      ;
    size_t l = ansi_mblen(spacer_ptr + 1);
    if (r >= l) {
      ncplane_cursor_move_yx(n, 0, ui->ncol - l);
      ncplane_addastr(n, spacer_ptr + 1);
    }
  }

  xfree(path_buf);
  xfree(file);
}

/* TODO: make the following two functions return the length of the output
 * (and make the callers use it) (on 2022-10-29) */
/* TODO: use these in the default infoline drawer (on 2022-10-29) */

// max_len is not a strict upper bound, but we try to make path as short as
// possible path probably shouldn't end with /
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
