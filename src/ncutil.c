#include "ncutil.h"

#include "log.h"
#include "macros.h"
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

static inline bool parse_number(const char **str, const char *end, int *num) {
  const char *ptr = *str;
  int acc = 0;

  if (unlikely((*ptr != '[' && *ptr != ';') || ptr + 1 >= end ||
               !isdigit(*++ptr)))
    return false;

  for (; likely(ptr < end) && isdigit(*ptr); ptr++)
    acc = acc * 10 + *ptr - '0';

  *str = ptr;
  *num = acc;

  return true;
}

const char *ncplane_set_ansi_attrs(struct ncplane *n, const char *str,
                                   const char *end) {
  assert(*str == '\033');
  str++;

  if (str < end && *(str + 1) == 'm') {
    normal(n);
    return str + 2;
  }

  int num;
  while (str < end && *str != 'm') {
    if (!parse_number(&str, end, &num)) {
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
        if (!parse_number(&str, end, &op)) {
          goto err;
        }
        switch (op) {
        case 5: {
          int p;
          if (!parse_number(&str, end, &p)) {
            goto err;
          }
          ncplane_set_fg_palindex(n, p);
        } break;
        case 2: {
          int r, g, b;
          if (!parse_number(&str, end, &r) || !parse_number(&str, end, &g) ||
              !parse_number(&str, end, &b)) {
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
        if (!parse_number(&str, end, &op)) {
          goto err;
        }
        switch (op) {
        case 5: {
          int p;
          if (!parse_number(&str, end, &p)) {
            goto err;
          }
          ncplane_set_bg_palindex(n, p);
        } break;
        case 2: {
          int r, g, b;
          if (!parse_number(&str, end, &r) || !parse_number(&str, end, &g) ||
              !parse_number(&str, end, &b)) {
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

  if (!*str)
    goto err;

ret:
  return str + 1;

err:
  normal(n);
  log_error("malformed/unsupported ansi escape");
  // seek to m?
  goto ret;
}

int ncplane_putnstr_ansi_yx(struct ncplane *n, int y, int x, size_t s,
                            const char *gclusters) {
  int ret = 0;
  ncplane_cursor_move_yx(n, y, x);
  const char *ptr = gclusters;
  const char *end = gclusters + s;
  while (ptr < end) {
    if (*ptr == '\033') {
      ptr = ncplane_set_ansi_attrs(n, ptr, end);
    } else {
      const char *cur;
      for (cur = ptr; ptr < end && *ptr != '\033'; ptr++) {
      }
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
      ptr = ncplane_set_ansi_attrs(n, ptr, end);
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

size_t ansi_mblen(const char *ptr) {
  const char *end = ptr + strlen(ptr);
  size_t len = 0;
  while (ptr < end) {
    if (*ptr == '\033') {
      while (ptr < end && *ptr != 'm')
        ptr++;
      if (ptr < end)
        ptr++; // skip 'm'
    } else {
      int l = mbtowc(NULL, ptr, end - ptr);
      if (l < 0)
        return len;
      ptr += l;
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
