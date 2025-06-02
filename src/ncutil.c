#include "ncutil.h"

#include "log.h"
#include "stc/cstr.h"

#include <curses.h>
#include <notcurses/notcurses.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>

static inline void normal(struct ncplane *n) {
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
}

static inline bool parse_number(const char **s, int *num) {
  const char *p = *s;
  int acc = 0;
  if ((*p != '[' && *p != ';') || !isdigit(*++p)) {
    return false;
  }
  while (isdigit(*p)) {
    acc = acc * 10 + *p - '0';
    p++;
  }
  *s = p;
  *num = acc;
  return true;
}

const char *ncplane_set_ansi_attrs(struct ncplane *n, const char *s) {
  assert(*s == '\033');
  s++;

  if (*s && *(s + 1) == 'm') {
    normal(n);
    return s + 2;
  }

  int num;
  while (*s && *s != 'm') {
    if (!parse_number(&s, &num)) {
      goto err;
    }

    if (num >= 30 && num <= 37) {
      ncplane_set_fg_palindex(n, num - 30);
    } else if (num >= 40 && num <= 47) {
      ncplane_set_bg_palindex(n, num - 40);
    } else {
      switch (num) {
      case 0:
        normal(n);
        break;
      case 1:
        ncplane_on_styles(n, NCSTYLE_BOLD);
        break;
      case 2:
        /* not supported by notcurses */
        ncplane_on_styles(n, WA_DIM);
        break;
      case 3:
        ncplane_on_styles(n, NCSTYLE_ITALIC);
        break;
      case 4:
        ncplane_on_styles(n, NCSTYLE_UNDERLINE);
        break;
      case 5:
        /* not supported by notcurses */
        // ncplane_on_styles(w, NCSTYLE_BLINK);
        break;
      case 6: /* nothing */
        break;
      case 7:
        /* not supported, needs workaround */
        // ncplane_on_styles(w, NCSTYLE_REVERSE);
        break;
      case 8:
        /* not supported by notcurses */
        // ncplane_on_styles(w, NCSTYLE_INVIS);
        break;
      case 9: /* strikethrough */
        ncplane_on_styles(n, NCSTYLE_STRUCK);
        break;
      case 22:
        ncplane_off_styles(n, NCSTYLE_BOLD);
        break;
      case 24:
        ncplane_off_styles(n, NCSTYLE_UNDERLINE);
        break;
      case 38: {
        int op;
        if (!parse_number(&s, &op)) {
          goto err;
        }
        switch (op) {
        case 5: {
          int p;
          if (!parse_number(&s, &p)) {
            goto err;
          }
          ncplane_set_fg_palindex(n, p);
        } break;
        case 2: {
          int r, g, b;
          if (!parse_number(&s, &r) || !parse_number(&s, &g) ||
              !parse_number(&s, &b)) {
            goto err;
          }
          ncplane_set_fg_rgb8(n, r, g, b);
        } break;
        default:
          goto err;
        }
      } break;
      case 39:
        ncplane_set_fg_default(n);
        break;
      case 48: {
        int op;
        if (!parse_number(&s, &op)) {
          goto err;
        }
        switch (op) {
        case 5: {
          int p;
          if (!parse_number(&s, &p)) {
            goto err;
          }
          ncplane_set_bg_palindex(n, p);
        } break;
        case 2: {
          int r, g, b;
          if (!parse_number(&s, &r) || !parse_number(&s, &g) ||
              !parse_number(&s, &b)) {
            goto err;
          }
          ncplane_set_bg_rgb8(n, r, g, b);
        } break;
        default:
          goto err;
        }
      } break;
      case 49:
        ncplane_set_bg_default(n);
        break;
      }
    }
  }

  if (!*s) {
    goto err;
  }

ret:
  return s + 1;

err:
  normal(n);
  log_error("malformed/unsupported ansi escape");
  // seek to m?
  goto ret;
}

int ncplane_putcs_ansi_yx(struct ncplane *n, int y, int x, csview cs) {
  int ret = 0;
  ncplane_cursor_move_yx(n, y, x);
  const char *ptr = cs.buf;
  const char *end = cs.buf + cs.size;
  while (ptr < end) {
    if (*ptr == '\033') {
      ptr = ncplane_set_ansi_attrs(n, ptr);
    } else {
      const char *cur;
      for (cur = ptr; ptr < end && *ptr != '\033'; ptr++)
        ;
      int m = ncplane_putnstr(n, ptr - cur, cur);
      if (m < 0) {
        // EOL/error
        ret -= m;
        break;
      }
      ret += m;
    }
  }
  return ret;
}

int ncplane_putlcs_ansi_yx(struct ncplane *n, int y, int x, size_t s,
                           csview cs) {
  int ret = 0;
  ncplane_cursor_move_yx(n, y, x);
  const char *ptr = cs.buf;
  const char *end = cs.buf + cs.size;
  while (ptr < end) {
    if (*ptr == '\033') {
      ptr = ncplane_set_ansi_attrs(n, ptr);
    } else {
      const char *cur;
      for (cur = ptr; ptr < end && *ptr != '\033'; ptr++)
        ;
      int m = ncplane_putnstr(n, ptr - cur, cur);
      if (m < 0) {
        // EOL/error
        ret -= m;
        break;
      }
      ret += m;
      if (ret >= (int)s) {
        break;
      }
    }
  }
  return ret;
}

size_t ansi_mblen(const char *s) {
  size_t len = 0;
  while (*s) {
    if (*s == '\033') {
      while (*s && *s != 'm') {
        s++;
      }
      if (*s == 'm') {
        s++;
      }
    } else {
      int l = mbtowc(NULL, s, MB_LEN_MAX);
      s += l;
      len++;
    }
  }
  return len;
}

// for ffi purposes

int ncplane_putchar_yx_(struct ncplane *n, int y, int x, char c) {
  nccell ce =
      NCCELL_INITIALIZER((uint32_t)c, ncplane_styles(n), ncplane_channels(n));
  return ncplane_putc_yx(n, y, x, &ce);
}

int ncplane_putstr_yx_(struct ncplane *n, int y, int x, const char *gclusters) {
  int ret = 0;
  while (*gclusters) {
    size_t wcs;
    int cols = ncplane_putegc_yx(n, y, x, gclusters, &wcs);
    // fprintf(stderr, "wrote %.*s %d cols %zu bytes\n", (int)wcs, gclusters,
    // cols, wcs);
    if (cols < 0) {
      return -ret;
    }
    if (wcs == 0) {
      break;
    }
    // after the first iteration, just let the cursor code control where we
    // print, so that scrolling is taken into account
    y = -1;
    x = -1;
    gclusters += wcs;
    ret += cols;
  }
  return ret;
}

int notcurses_render_(struct notcurses *nc) {
  struct ncplane *stdn = notcurses_stdplane(nc);
  if (ncpile_render(stdn)) {
    return -1;
  }
  return ncpile_rasterize(stdn);
}
