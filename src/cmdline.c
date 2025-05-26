#include "cmdline.h"

#include "config.h"
#include "macros_defs.h"
#include "memory.h"
#include "ncutil.h"
#include "profiling.h"
#include "stcutil.h"
#include "ui.h"

#include "stc/zsview.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define get_ui(cmdline_) container_of(cmdline_, struct Ui, cmdline)

#define VSTR_INIT(vec, c)                                                      \
  do {                                                                         \
    (vec).str = xmalloc(((c) + 1) * sizeof *(vec).str);                        \
    (vec).cap = c;                                                             \
    (vec).str[0] = 0;                                                          \
    (vec).len = 0;                                                             \
  } while (0)

#define ENSURE_CAPACITY(vec, _sz)                                              \
  do {                                                                         \
    const size_t v_sz = _sz;                                                   \
    if ((vec).cap < v_sz) {                                                    \
      while ((vec).cap < v_sz)                                                 \
        (vec).cap *= 2;                                                        \
      (vec).str = xrealloc((vec).str, ((vec).cap * 2 + 1) * sizeof *vec.str);  \
    }                                                                          \
  } while (0)

#define ENSURE_SPACE(vec, sz) ENSURE_CAPACITY(vec, (size_t)(vec).len + sz)

#define SHIFT_RIGHT(t, _sz)                                                    \
  do {                                                                         \
    const size_t sz = _sz;                                                     \
    ENSURE_SPACE((t)->right, sz);                                              \
    memmove((t)->right.str + sz, (t)->right.str,                               \
            ((t)->right.len + 1) * sizeof *(t)->right.str);                    \
    memcpy((t)->right.str, (t)->left.str + (t)->left.len - sz,                 \
           sz * sizeof *(t)->right.str);                                       \
    (t)->right.len += sz;                                                      \
    (t)->left.len -= sz;                                                       \
    (t)->left.str[(t)->left.len] = 0;                                          \
  } while (0)

#define SHIFT_LEFT(t, _sz)                                                     \
  do {                                                                         \
    const size_t sz = _sz;                                                     \
    ENSURE_SPACE((t)->left, sz);                                               \
    memcpy((t)->left.str + (t)->left.len, (t)->right.str,                      \
           sz * sizeof *(t)->left.str);                                        \
    memmove((t)->right.str, (t)->right.str + sz,                               \
            ((t)->right.len - sz + 1) * sizeof *(t)->right.str);               \
    (t)->right.len -= sz;                                                      \
    (t)->left.len += sz;                                                       \
    (t)->left.str[(t)->left.len] = 0;                                          \
  } while (0)

void cmdline_init(Cmdline *c) {
  VSTR_INIT(c->prefix, 8);
  VSTR_INIT(c->left, 8);
  VSTR_INIT(c->right, 8);
  VSTR_INIT(c->buf, 8);
  c->overwrite = false;
  PROFILE("history_load", history_load(&c->history, cstr_zv(&cfg.historypath));)
}

void cmdline_deinit(Cmdline *c) {
  if (!c) {
    return;
  }

  history_write(&c->history, cstr_zv(&cfg.historypath), cfg.histsize);
  history_deinit(&c->history);

  fputs("\033[2 q", stdout);

  xfree(c->prefix.str);
  xfree(c->left.str);
  xfree(c->right.str);
  xfree(c->buf.str);
}

bool cmdline_prefix_set(Cmdline *c, zsview zv) {
  ENSURE_CAPACITY(c->prefix, zv.size);
  // copy trailing nul, extra space is reserved
  memcpy(c->prefix.str, zv.str, zv.size + 1);
  c->prefix.len = zv.size;
  return true;
}

bool cmdline_insert(Cmdline *c, zsview zv) {
  if (c->prefix.len == 0) {
    return false;
  }

  ENSURE_SPACE(c->left, 1);
  mbtowc(c->left.str + c->left.len, zv.str, zv.size);
  c->left.len++;
  c->left.str[c->left.len] = 0;
  if (c->overwrite && c->right.len > 0) {
    memmove(c->right.str, c->right.str + 1,
            c->right.len * sizeof *c->right.str);
    c->right.len--;
  }
  return true;
}

bool cmdline_toggle_overwrite(Cmdline *c) {
  c->overwrite = !c->overwrite;
  return true;
}

bool cmdline_delete(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  c->left.str[c->left.len - 1] = 0;
  c->left.len--;
  return true;
}

bool cmdline_delete_right(Cmdline *c) {
  if (c->prefix.len == 0 || c->right.len == 0) {
    return false;
  }

  memmove(c->right.str, c->right.str + 1, c->right.len * sizeof *c->right.str);
  c->right.len--;
  return true;
}

bool cmdline_delete_word(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  int32_t i = c->left.len - 1;
  while (i > 0 && iswspace(c->left.str[i])) {
    i--;
  }
  while (i > 0 && c->left.str[i] == '/' && !iswspace(c->left.str[i - 1])) {
    i--;
  }
  while (i > 0 &&
         !(iswspace(c->left.str[i - 1]) || c->left.str[i - 1] == '/')) {
    i--;
  }
  c->left.len = i;
  c->left.str[i] = 0;
  return true;
}

bool cmdline_delete_line_left(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  c->left.len = 0;
  c->left.str[c->left.len] = 0;
  return true;
}

/* pass a ct argument to move over words? */
bool cmdline_left(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  SHIFT_RIGHT(c, 1);
  return true;
}

bool cmdline_word_left(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  int32_t i = c->left.len;
  if (i > 0 && iswpunct(c->left.str[i - 1])) {
    i--;
  }
  if (i > 0 && iswspace(c->left.str[i - 1])) {
    while (i > 0 && iswspace(c->left.str[i - 1])) {
      i--;
    }
  } else {
    while (i > 0 &&
           !(iswspace(c->left.str[i - 1]) || iswpunct(c->left.str[i - 1]))) {
      i--;
    }
  }
  SHIFT_RIGHT(c, c->left.len - i);
  return true;
}

bool cmdline_word_right(Cmdline *c) {
  if (c->prefix.len == 0 || c->right.len == 0) {
    return false;
  }

  uint32_t i = 0;
  if (i < c->right.len && iswpunct(c->right.str[i])) {
    i++;
  }
  if (i < c->right.len && iswspace(c->right.str[i])) {
    while (i < c->right.len && iswspace(c->right.str[i])) {
      i++;
    }
  } else {
    while (i < c->right.len &&
           !(iswspace(c->right.str[i]) || iswpunct(c->right.str[i]))) {
      i++;
    }
  }
  SHIFT_LEFT(c, i);
  return true;
}

bool cmdline_right(Cmdline *c) {
  if (c->prefix.len == 0 || c->right.len == 0) {
    return false;
  }

  SHIFT_LEFT(c, 1);
  return true;
}

bool cmdline_home(Cmdline *c) {
  if (c->prefix.len == 0 || c->left.len == 0) {
    return false;
  }

  SHIFT_RIGHT(c, c->left.len);
  return true;
}

bool cmdline_end(Cmdline *c) {
  if (c->prefix.len == 0 || c->right.len == 0) {
    return false;
  }

  SHIFT_LEFT(c, c->right.len);
  return true;
}

bool cmdline_clear(Cmdline *c) {
  c->left.str[0] = 0;
  c->left.len = 0;
  c->right.str[0] = 0;
  c->right.len = 0;
  c->overwrite = false;
  history_reset(&c->history);
  notcurses_cursor_disable(get_ui(c)->nc);
  ui_menu_hide(get_ui(c));
  ui_redraw(get_ui(c), REDRAW_CMDLINE | REDRAW_MENU);
  return true;
}

bool cmdline_set(Cmdline *c, zsview left, zsview right) {
  if (!zsview_is_empty(left)) {
    ENSURE_SPACE(c->left, left.size);
    size_t n = mbstowcs(c->left.str, left.str, c->left.cap + 1);
    if (n == (size_t)-1) {
      c->left.len = 0;
      c->left.str[0] = 0;
    } else {
      c->left.len = n;
    }
  } else {
    c->left.len = 0;
    c->left.str[0] = 0;
  }
  if (!zsview_is_empty(right)) {
    ENSURE_SPACE(c->right, right.size);
    size_t n = mbstowcs(c->right.str, right.str, c->right.cap + 1);
    if (n == (size_t)-1) {
      c->right.len = 0;
      c->right.str[0] = 0;
    } else {
      c->right.len = n;
    }
  } else {
    c->right.len = 0;
    c->right.str[0] = 0;
  }
  return true;
}

zsview cmdline_get(Cmdline *c) {
  c->buf.str[0] = 0;
  if (c->prefix.len != 0) {
    ENSURE_SPACE(c->buf, (size_t)(c->left.len + c->right.len) * MB_CUR_MAX);
    size_t n = wcstombs(c->buf.str, c->left.str, c->left.len * MB_CUR_MAX);
    if (n == (size_t)-1) {
      return c_zv("");
    }

    size_t m =
        wcstombs(c->buf.str + n, c->right.str, c->right.len * MB_CUR_MAX);
    if (m == (size_t)-1) {
      return c_zv("");
    }

    c->buf.str[n + m] = 0;
    c->buf.len = n + m;
  }
  return zsview_from_n(c->buf.str, c->buf.len);
}

uint32_t cmdline_draw(Cmdline *c, struct ncplane *n) {
  ncplane_erase(n);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);
  ncplane_cursor_yx(n, 0, 0);

  unsigned int ncol;
  int offset;
  ncplane_dim_yx(n, NULL, &ncol);

  uint32_t ret = 0;
  ret += ncplane_put_str_ansi_yx(n, 0, 0, c->prefix.str);
  ncol -= ret;

  if (c->right.len == 0) {
    offset = c->left.len - ncol + 1;
  } else if (c->right.len > (uint32_t)ncol / 2) {
    offset = c->left.len - ncol + ncol / 2 + 1;
  } else {
    offset = c->left.len - ncol + c->right.len + 1;
  }
  ret += ncplane_putwstr(n, c->left.str + (offset > 0 ? offset : 0));
  ncplane_putwstr(n, c->right.str);

  fputs(c->right.len == 0 ? "\033[2 q"
        : c->overwrite    ? "\033[4 q"
                          : "\033[6 q",
        stdout); // block/bar cursor

  return ret;
}
