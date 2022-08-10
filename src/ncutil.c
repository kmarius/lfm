#include <curses.h>

#include "log.h"
#include "ncutil.h"


static void wansi_matchattr(struct ncplane *w, uint16_t a)
{
  if (a <= 9) {
    switch (a) {
      case 0:
        ncplane_set_styles(w, NCSTYLE_NONE);
        ncplane_set_fg_default(w);
        ncplane_set_bg_default(w);
        break;
      case 1:
        ncplane_on_styles(w, NCSTYLE_BOLD);
        break;
      case 2:
        /* not supported by notcurses */
        ncplane_on_styles(w, WA_DIM);
        break;
      case 3:
        ncplane_on_styles(w, NCSTYLE_ITALIC);
        break;
      case 4:
        ncplane_on_styles(w, NCSTYLE_UNDERLINE);
        break;
      case 5:
        /* not supported by notcurses */
        ncplane_on_styles(w, NCSTYLE_BLINK);
        break;
      case 6: /* nothing */
        break;
      case 7:
        /* not supported, needs workaround */
        ncplane_on_styles(w, NCSTYLE_REVERSE);
        break;
      case 8:
        /* not supported by notcurses */
        ncplane_on_styles(w, NCSTYLE_INVIS);
        break;
      case 9: /* strikethrough */
        ncplane_on_styles(w, NCSTYLE_STRUCK);
        break;
      default:
        break;
    }
  } else if (a >= 30 && a <= 37) {
    ncplane_set_fg_palindex(w, a-30);
  } else if (a >= 40 && a <= 47) {
    ncplane_set_bg_palindex(w, a-40);
  }
}


// Consooms ansi color escape sequences and sets ATTRS
// should be called with a pointer at \033
const char *ansi_consoom(struct ncplane *w, const char *s)
{
  char c;
  uint16_t acc = 0;
  uint16_t nnums = 0;
  uint16_t nums[6];
  s++; // first char guaranteed to be \033
  if (*s != '[') {
    log_error("there should be a [ here");
    return s;
  }
  s++;
  bool fin = false;
  while (!fin) {
    switch (c = *s) {
      case 'm':
        nums[nnums++] = acc;
        acc = 0;
        fin = true;
        break;
      case ';':
        nums[nnums++] = acc;
        acc = 0;
        break;
      case '\0':
        log_error("escape ended prematurely");
        return s;
      default:
        if (!(c >= '0' && c <= '9')) {
          log_error("not a number? %c", c);
        }
        acc = 10 * acc + (c - '0');
    }
    if (nnums > 5) {
      log_error("malformed ansi: too many numbers");
      /* TODO: seek to 'm' (on 2021-07-29) */
      return s;
    }
    s++;
  }
  if (nnums == 0) {
    /* highlight actually does this, but it will be recognized as \e[0m instead */
  } else if (nnums == 1) {
    wansi_matchattr(w, nums[0]);
  } else if (nnums == 2) {
    wansi_matchattr(w, nums[0]);
    wansi_matchattr(w, nums[1]);
  } else if (nnums == 3) {
    if (nums[0] == 38 && nums[1] == 5) {
      ncplane_set_fg_palindex(w, nums[2]);
    } else if (nums[0] == 48 && nums[1] == 5) {
      log_error("trying to set background color per ansi code");
    }
  } else if (nnums == 4) {
    wansi_matchattr(w, nums[0]);
    ncplane_set_fg_palindex(w, nums[3]);
  } else if (nnums == 5) {
    log_error("using unsupported ansi code with 5 numbers");
    /* log_debug("%d %d %d %d %d", nums[0], nums[1], nums[2],
     * nums[3], nums[4]);
     */
  } else if (nnums == 6) {
    log_error("using unsupported ansi code with 6 numbers");
    /* log_debug("%d %d %d %d %d %d", nums[0], nums[1], nums[2],
     * nums[3], nums[4], nums[5]); */
  }

  return s;
}

void ansi_addstr(struct ncplane *n, const char *s)
{
  while (*s) {
    if (*s == '\033') {
      s = ansi_consoom(n, s);
    } else {
      const char *c;
      for (c = s; *s != 0 && *s != '\033'; s++);
      if (ncplane_putnstr(n, s-c, c) == -1) {
        return; // EOL
      }
    }
  }
}
