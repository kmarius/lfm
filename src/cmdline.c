#include "cmdline.h"
#include "history.h"

#define STC_CSTR_UTF8
#include "stc/cstr.h"

#define STC_CSTR_UTF8
#include "stc/zsview.h"

#include "config.h"
#include "macros.h"
#include "ncutil.h"
#include "profiling.h"
#include "stcutil.h"
#include "ui.h"

#include <stdint.h>

#define get_ui(cmdline_) container_of(cmdline_, struct Ui, cmdline)

void cmdline_init(Cmdline *c) {
  c->prefix = cstr_init();
  c->left = cstr_init();
  c->right = cstr_init();
  c->buf = cstr_with_capacity(32);
  c->overwrite = false;
  PROFILE("history_load",
          { history_load(&c->history, cstr_zv(&cfg.historypath)); })
}

void cmdline_deinit(Cmdline *c) {
  if (c == NULL)
    return;

  history_write(&c->history, cstr_zv(&cfg.historypath), cfg.histsize);
  history_deinit(&c->history);

  fputs("\033[2 q", stdout);

  cstr_drop(&c->prefix);
  cstr_drop(&c->buf);
  cstr_drop(&c->left);
  cstr_drop(&c->right);
}

bool cmdline_prefix_set(Cmdline *c, zsview zv) {
  cstr_assign_zv(&c->prefix, zv);
  return true;
}

bool cmdline_insert(Cmdline *c, zsview zv) {
  if (cstr_is_empty(&c->prefix))
    return false;

  csview sv = zsview_u8_subview(zv, 0, 1);
  cstr_append_sv(&c->left, sv);

  if (c->overwrite && !cstr_is_empty(&c->right)) {
    cstr_u8_erase(&c->right, 0, 1);
  }
  return true;
}

bool cmdline_toggle_overwrite(Cmdline *c) {
  c->overwrite = !c->overwrite;
  return true;
}

bool cmdline_delete(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  cstr_pop(&c->left);
  return true;
}

bool cmdline_delete_right(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->right))
    return false;

  cstr_u8_erase(&c->right, 0, 1);
  return true;
}

bool cmdline_delete_word(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  zsview left = cstr_zv(&c->left);
  int i = left.size;
  if (i > 0 && ispunct(left.str[i - 1])) {
    i--;
  }
  if (i > 0 && isspace(left.str[i - 1])) {
    while (i > 0 && isspace(left.str[i - 1])) {
      i--;
    }
  } else {
    while (i > 0 && !(isspace(left.str[i - 1]) || ispunct(left.str[i - 1]))) {
      i--;
    }
  }

  _cstr_set_size(&c->left, i);

  return true;
}

bool cmdline_delete_line_left(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  cstr_clear(&c->left);
  return true;
}

/* pass a ct argument to move over words? */
bool cmdline_left(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  zsview tail = cstr_u8_tail(&c->left, 1);
  cstr_insert_sv(&c->right, 0, zsview_sv(tail));
  cstr_pop(&c->left);
  return true;
}

bool cmdline_word_left(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  zsview left = cstr_zv(&c->left);
  int i = left.size;
  if (i > 0 && ispunct(left.str[i - 1])) {
    i--;
  }
  if (i > 0 && isspace(left.str[i - 1])) {
    while (i > 0 && isspace(left.str[i - 1])) {
      i--;
    }
  } else {
    while (i > 0 && !(isspace(left.str[i - 1]) || ispunct(left.str[i - 1]))) {
      i--;
    }
  }

  cstr_insert_zv(&c->right, 0, zsview_tail(left, left.size - i));
  _cstr_set_size(&c->left, i);

  return true;
}

bool cmdline_word_right(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->right))
    return false;

  zsview right = cstr_zv(&c->right);

  int i = 0;
  if (i < right.size && iswpunct(right.str[i])) {
    i++;
  }
  if (i < right.size && iswspace(right.str[i])) {
    while (i < right.size && iswspace(right.str[i])) {
      i++;
    }
  } else {
    while (i < right.size &&
           !(iswspace(right.str[i]) || iswpunct(right.str[i]))) {
      i++;
    }
  }

  cstr_append_sv(&c->left, cstr_subview(&c->right, 0, i));
  cstr_erase(&c->right, 0, i);

  return true;
}

bool cmdline_right(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->right))
    return false;

  csview cs = cstr_u8_subview(&c->right, 0, 1);
  cstr_append_sv(&c->left, cs);
  cstr_erase(&c->right, 0, cs.size);

  return true;
}

bool cmdline_home(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->left))
    return false;

  cstr_insert_sv(&c->right, 0, cstr_sv(&c->left));
  cstr_clear(&c->left);
  return true;
}

bool cmdline_end(Cmdline *c) {
  if (cstr_is_empty(&c->prefix) || cstr_is_empty(&c->right))
    return false;

  cstr_append_s(&c->left, c->right);
  cstr_clear(&c->right);
  return true;
}

bool cmdline_clear(Cmdline *c) {
  cstr_clear(&c->prefix);
  cstr_clear(&c->buf);
  cstr_clear(&c->left);
  cstr_clear(&c->right);
  c->overwrite = false;
  history_reset(&c->history);
  notcurses_cursor_disable(get_ui(c)->nc);
  ui_menu_hide(get_ui(c));
  ui_redraw(get_ui(c), REDRAW_CMDLINE | REDRAW_MENU);
  return true;
}

bool cmdline_set(Cmdline *c, zsview left, zsview right) {
  if (!zsview_is_empty(left))
    cstr_assign_zv(&c->left, left);
  if (!zsview_is_empty(right))
    cstr_assign_zv(&c->right, right);
  return true;
}

// copied into the buffer on every call
zsview cmdline_get(Cmdline *c) {
  cstr_copy(&c->buf, c->left);
  cstr_append_s(&c->buf, c->right);
  return cstr_zv(&c->buf);
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
  ret += ncplane_put_str_ansi_yx(n, 0, 0, cstr_str(&c->prefix));
  ncol -= ret;

  unsigned left_len = cstr_u8_size(&c->left);
  unsigned right_len = cstr_u8_size(&c->right);

  // scroll some if the line is too long
  if (right_len == 0) {
    offset = left_len - ncol + 1;
  } else if (right_len > ncol / 2) {
    offset = left_len - ncol + ncol / 2 + 1;
  } else {
    offset = left_len - ncol + right_len + 1;
  }

  if (offset > 0) {
    offset = cstr_u8_to_index(&c->left, offset);
  } else {
    offset = 0;
  }

  ret += ncplane_putstr(n, cstr_str(&c->left) + +offset);
  ncplane_putstr(n, cstr_str(&c->right));

  fputs(right_len == 0 ? "\033[2 q"
        : c->overwrite ? "\033[4 q"
                       : "\033[6 q",
        stdout); // block/bar cursor

  return ret;
}
