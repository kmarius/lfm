#include "cmdline.h"
#include "log.h"

#define STC_CSTR_UTF8
#include "stc/cstr.h"

#define STC_CSTR_UTF8
#include "stc/zsview.h"

#include "config.h"
#include "history.h"
#include "macros.h"
#include "ncutil.h"
#include "profiling.h"
#include "stcutil.h"
#include "ui.h"

#include <stdint.h>

#define get_ui(cmdline_) container_of(cmdline_, struct Ui, cmdline)
#define Self Cmdline

void cmdline_init(Self *self) {
  self->prefix = cstr_init();
  self->left = cstr_init();
  self->right = cstr_init();
  self->buf = cstr_with_capacity(32);
  self->overwrite = false;
  PROFILE("history_load",
          { history_load(&self->history, cstr_zv(&cfg.historypath)); })
}

void cmdline_deinit(Self *self) {
  if (self == NULL)
    return;

  history_write(&self->history, cstr_zv(&cfg.historypath), cfg.histsize);
  history_deinit(&self->history);

  fputs("\033[2 q", stdout);

  cstr_drop(&self->prefix);
  cstr_drop(&self->buf);
  cstr_drop(&self->left);
  cstr_drop(&self->right);
}

bool cmdline_prefix_set(Self *self, zsview zv) {
  cstr_assign_zv(&self->prefix, zv);
  return true;
}

bool cmdline_insert(Self *self, zsview zv) {
  if (cstr_is_empty(&self->prefix))
    return false;

  csview sv = zsview_u8_subview(zv, 0, 1);
  cstr_append_sv(&self->left, sv);

  if (self->overwrite && !cstr_is_empty(&self->right)) {
    cstr_u8_erase(&self->right, 0, 1);
  }
  return true;
}

bool cmdline_toggle_overwrite(Self *self) {
  self->overwrite = !self->overwrite;
  return true;
}

bool cmdline_delete(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  cstr_pop(&self->left);
  return true;
}

bool cmdline_delete_right(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->right))
    return false;

  cstr_u8_erase(&self->right, 0, 1);
  return true;
}

bool cmdline_delete_line_left(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  cstr_clear(&self->left);
  return true;
}

/* pass a ct argument to move over words? */
bool cmdline_left(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  zsview tail = cstr_u8_tail(&self->left, 1);
  cstr_insert_zv(&self->right, 0, tail);
  cstr_pop(&self->left);
  return true;
}

bool cmdline_right(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->right))
    return false;

  csview cs = cstr_u8_subview(&self->right, 0, 1);
  cstr_append_sv(&self->left, cs);
  cstr_erase(&self->right, 0, cs.size);
  return true;
}

bool cmdline_home(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  cstr_append_s(&self->left, self->right);
  cstr_clear(&self->right);
  c_swap(&self->left, &self->right);
  return true;
}

bool cmdline_end(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->right))
    return false;

  cstr_append_s(&self->left, self->right);
  cstr_clear(&self->right);
  return true;
}

bool cmdline_delete_word(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  zsview left = cstr_zv(&self->left);
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

  _cstr_set_size(&self->left, i);

  return true;
}

bool cmdline_word_left(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->left))
    return false;

  zsview left = cstr_zv(&self->left);
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

  cstr_insert_zv(&self->right, 0, zsview_tail(left, left.size - i));
  _cstr_set_size(&self->left, i);

  return true;
}

bool cmdline_word_right(Self *self) {
  if (cstr_is_empty(&self->prefix) || cstr_is_empty(&self->right))
    return false;

  zsview right = cstr_zv(&self->right);

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

  cstr_append_sv(&self->left, cstr_subview(&self->right, 0, i));
  cstr_erase(&self->right, 0, i);

  return true;
}

bool cmdline_clear(Self *self) {
  cstr_clear(&self->prefix);
  cstr_clear(&self->buf);
  cstr_clear(&self->left);
  cstr_clear(&self->right);
  self->overwrite = false;
  history_reset(&self->history);
  notcurses_cursor_disable(get_ui(self)->nc);
  ui_menu_hide(get_ui(self));
  ui_redraw(get_ui(self), REDRAW_CMDLINE | REDRAW_MENU);
  return true;
}

bool cmdline_set(Self *self, zsview left, zsview right) {
  if (!zsview_is_empty(left))
    cstr_assign_zv(&self->left, left);
  if (!zsview_is_empty(right))
    cstr_assign_zv(&self->right, right);
  return true;
}

// copied into the buffer on every call
zsview cmdline_get(Self *self) {
  cstr_copy(&self->buf, self->left);
  cstr_append_s(&self->buf, self->right);
  return cstr_zv(&self->buf);
}

int cmdline_draw(Self *self, struct ncplane *n) {
  unsigned int ncol;
  ncplane_dim_yx(n, NULL, &ncol);

  ncplane_erase(n);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  unsigned xpos = 0;
  xpos += ncplane_putcstr_ansi_yx(n, 0, 0, &self->prefix);
  unsigned remaining = ncol - xpos;

  unsigned left_len = cstr_u8_size(&self->left);
  unsigned right_len = cstr_u8_size(&self->right);

  // scroll some if the line is too long
  int offset;
  if (right_len == 0) {
    offset = left_len - remaining + 1;
  } else if (right_len > remaining / 2) {
    offset = left_len - remaining + remaining / 2 + 1;
  } else {
    offset = left_len - remaining + right_len;
  }

  if (offset < 0) {
    offset = 0;
  } else {
    xpos += ncplane_putnstr(n, 1, cfg.truncatechar);
    offset++;
  }

  zsview tail = cstr_u8_tail(&self->left, left_len - offset);
  xpos += ncplane_putzv(n, tail);
  ncplane_putcstr(n, &self->right);
  if (xpos + right_len > ncol) {
    ncplane_putnstr_yx(n, 0, ncol - 1, 1, cfg.truncatechar);
  }

  fputs(right_len == 0    ? "\033[2 q"
        : self->overwrite ? "\033[4 q"
                          : "\033[6 q",
        stdout); // block/bar cursor

  return xpos;
}
