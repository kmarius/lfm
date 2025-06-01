#include "infoline.h"

#include "config.h"
#include "lfm.h"
#include "log.h"
#include "macros.h"
#include "ncutil.h"
#include "spinner.h"
#include "stc/csview.h"
#include "stc/zsview.h"
#include "ui.h"

#include <curses.h>

#include <linux/limits.h>
#include <notcurses/notcurses.h>
#include <string.h>
#include <unistd.h>

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
  void *ptr; // data belonging to the placeholder, possibly a char *
} placeholders[PLACEHOLDERS_MAX] = {0};

// index positions of some placeholders
static struct {
  int file, path, spinner, spacer, mode;
} idx;

static inline void draw_custom(Ui *ui);
static inline void draw_default(Ui *ui);
static inline int shorten_path(zsview path, char *buf, int max_len);

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
  File *file = NULL;
  char file_buf[128] = {0};
  int file_len = 0;
  bool file_is_dir = false;

  char path_buf[PATH_MAX] = {0};

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
    file = fm_current_file(&to_lfm(ui)->fm);
    if (file != NULL) {
      file_len = zsview_u8_size(*file_name(file));
      file_is_dir = file_isdir(file);
    }
  }

  if (idx.path != 0) {
    const Dir *dir = fm_current_dir(&to_lfm(ui)->fm);

    int path_remaining = remaining - file_len;

    size_t buf_idx = 0;

    // replace $HOME with ~
    zsview path = cstr_zv(dir_path(dir));
    if (zsview_starts_with(path, home)) {
      path = zsview_from_pos(path, home_len);
      path_buf[buf_idx++] = '~';
      path_remaining--;
    }

    if (!dir_isroot(dir) && path_remaining > 0) {
      path_remaining--; // extra trailing '/', later
    }

    int u8_len;

    if (path_remaining <= 1) {
      strcpy(path_buf, cfg.truncatechar);
      buf_idx = strlen(path_buf);
      u8_len = 1;
    } else {
      u8_len = shorten_path(path, &path_buf[buf_idx], path_remaining);
      buf_idx = strlen(path_buf);
    }

    // trailing slash
    if (!dir_isroot(dir)) {
      if (buf_idx < sizeof path_buf - 1) {
        path_buf[buf_idx] = '/';
        path_buf[buf_idx + 1] = 0;
        buf_idx++;
        u8_len++;
      }
    }

    placeholders[idx.path].replacement_len = u8_len;
    placeholders[idx.path].ptr = path_buf;
    remaining -= u8_len;
  }

  if (idx.file != 0) {
    if (file != NULL) {
      shorten_name(*file_name(file), file_buf, remaining, !file_is_dir);
      if (remaining < file_len) {
        file_len = remaining;
      }
    }
    // TODO: could be returned by shorten_file_name
    placeholders[idx.file].replacement_len = file_len;
    placeholders[idx.file].ptr = file_buf;
  }

  for (int i = 0; i < num_placeholders; i++) {
    switch (placeholders[i].c) {
    case 0:
      break;
    case 'f':
      ncplane_putstr(n, placeholders[i].ptr);
      break;
    case 'p':
      ncplane_putstr(n, placeholders[i].ptr);
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

  zsview path = cstr_zv(dir_path(dir));

  unsigned remaining;
  ncplane_cursor_yx(n, NULL, &remaining);
  remaining = ui->x - remaining;

  if (file != NULL) {
    remaining -= zsview_u8_size(*file_name(file));
  }

  ncplane_set_fg_palindex(n, COLOR_BLUE);
  if (zsview_starts_with(path, home)) {
    ncplane_putchar(n, '~');
    remaining--;
    path = zsview_from_pos(path, home_len);
  }

  if (!dir_isroot(dir)) {
    remaining--; // printing another '/' after path
  }

  char buf[PATH_MAX];
  shorten_path(path, buf, remaining);
  ncplane_putstr(n, buf);

  if (!dir_isroot(dir)) {
    ncplane_putchar(n, '/');
  }

  if (file != NULL) {
    ncplane_cursor_yx(n, NULL, &remaining);
    remaining = ui->x - remaining;
    ncplane_set_fg_default(n);
    shorten_name(*file_name(file), buf, remaining, !file_isdir(file));
    ncplane_putstr(n, buf);
  }
}

/* TODO: make the following two functions return the length of the output
 * (and make the callers use it) (on 2022-10-29) */

// max_len is not a strict upper bound, but we try to make path as short as
// possible. path probably shouldn't end with /
static inline int shorten_path(zsview path, char *buf, int max_len) {
  int trunc_len = strlen(cfg.truncatechar);
  int max = max_len;

  char *ptr = buf;

  // very short
  if (max_len == 2) {
    *ptr++ = '/';
    memcpy(ptr, cfg.truncatechar, trunc_len + 1);
    ptr += trunc_len;
    return 2;
  } else if (max_len == 1) {
    memcpy(ptr, cfg.truncatechar, trunc_len + 1);
    ptr += trunc_len;
    return 1;
  } else if (max_len == 0) {
    *ptr = 0;
    return 0;
  }

  int path_remaining_u8 = zsview_u8_size(path);
  if (path_remaining_u8 <= max_len) {
    memcpy(buf, path.str, path.size + 1);
    return path_remaining_u8;
  }

  zsview cur = path;
  while (path_remaining_u8 > 0 && path_remaining_u8 > max_len) {
    // first char is / on each iteration
    cur = zsview_from_pos(cur, 1);
    *ptr++ = '/';
    max_len--;
    path_remaining_u8--;

    const char *next = strchr(cur.str, '/');

    int len = 0;
    int u8_len;
    if (next != NULL) {
      len = next - cur.str;
      u8_len = csview_u8_size(zsview_subview(cur, 0, len));
    } else {
      len = cur.size;
      u8_len = path_remaining_u8;
    }

    path_remaining_u8 -= u8_len;

    if (path_remaining_u8 <= (int)max_len) {
      // Everything after the next component fits, we can print some of this
      // one

      // fill this many cells
      int m = max_len - path_remaining_u8;
      if (m >= 2) {
        csview cs = zsview_u8_subview(cur, 0, m - 1);
        memcpy(ptr, cs.buf, cs.size);
        ptr += cs.size;
        memcpy(ptr, cfg.truncatechar, trunc_len);
        ptr += trunc_len;
      } else {
        // space for just one
        csview cs = zsview_u8_subview(cur, 0, 1);
        memcpy(ptr, cs.buf, cs.size);
        ptr += cs.size;
      }
      max_len -= m;
    } else if (max_len == 1) {
      // way too little space, aborting
      memcpy(ptr, cfg.truncatechar, trunc_len + 1);
      return max;
    } else if (max_len == 0) {
      *ptr = 0;
      return max;
    } else {
      // otherwise, print one char only.
      csview cs = zsview_u8_subview(cur, 0, 1);
      memcpy(ptr, cs.buf, cs.size);
      ptr += cs.size;
      max_len--;
    }

    // advance to next /
    cur = zsview_from_pos(cur, len);
  }

  memcpy(ptr, cur.str, cur.size + 1);
  ptr += cur.size;

  return max;
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
