#include <ctype.h>
#include <curses.h>
#include <assert.h>
#include <limits.h>

#include "log.h"
#include "ncutil.h"

static inline void normal(struct ncplane *n)
{
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
}

static inline bool parse_number(const char **s, int *num)
{
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

const char *ncplane_set_ansi_attrs(struct ncplane *n, const char *s)
{
  assert(*s == '\033');
  s++;

  if (*s && *(s+1) == 'm') {
    normal(n);
    return s + 2;
  }

  int num;
  while (*s && *s != 'm') {
    if (!parse_number(&s, &num)) {
      goto err;
    }

    if (num >= 30 && num <= 37) {
      ncplane_set_fg_palindex(n, num-30);
    } else if (num >= 40 && num <= 47) {
      ncplane_set_bg_palindex(n, num-40);
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
        case 38:
          {
            int op;
            if (!parse_number(&s, &op)) {
              goto err;
            }
            switch (op) {
              case 5:
                {
                  int p;
                  if (!parse_number(&s, &p)) {
                    goto err;
                  }
                  ncplane_set_fg_palindex(n, p);
                }
                break;
              case 2:
                {
                  int r, g, b;
                  if (!parse_number(&s, &r)
                      || !parse_number(&s, &g)
                      || !parse_number(&s, &b)) {
                    goto err;
                  }
                  ncplane_set_fg_rgb8(n, r, g, b);
                }
                break;
              default:
                goto err;
            }
          }
          break;
        case 39:
          ncplane_set_fg_default(n);
          break;
        case 48:
          {
            int op;
            if (!parse_number(&s, &op)) {
              goto err;
            }
            switch (op) {
              case 5:
                {
                  int p;
                  if (!parse_number(&s, &p)) {
                    goto err;
                  }
                  ncplane_set_bg_palindex(n, p);
                }
                break;
              case 2:
                {
                  int r, g, b;
                  if (!parse_number(&s, &r)
                      || !parse_number(&s, &g)
                      || !parse_number(&s, &b)) {
                    goto err;
                  }
                  ncplane_set_bg_rgb8(n, r, g, b);
                }
                break;
              default:
                goto err;
            }
          }
          break;
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

void ncplane_addastr_yx(struct ncplane *n, int y, int x, const char *s)
{
  ncplane_cursor_move_yx(n, y, x);
  while (*s) {
    if (*s == '\033') {
      s = ncplane_set_ansi_attrs(n, s);
    } else {
      const char *c;
      for (c = s; *s != 0 && *s != '\033'; s++);
      if (ncplane_putnstr(n, s-c, c) == -1) {
        return; // EOL
      }
    }
  }
}

size_t ansi_mblen(const char *s)
{
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
