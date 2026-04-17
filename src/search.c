#include "search.h"

#include "config.h"
#include "dir.h"
#include "fm.h"
#include "lfm.h"
#include "stcutil.h"
#include "ui.h"
#include "util.h"

// empty re-enables
static inline void search_highlight(Ui *ui, zsview string) {
  if (!zsview_is_empty(string)) {
    cstr_assign_zv(&ui->search_string, string);
  }
  ui->highlight = cstr_zv(&ui->search_string);
  ui_redraw(ui, REDRAW_CURRENT);
}

// re-enable highlight with current search
static inline void search_rehighlight(Ui *ui) {
  if (!cstr_is_empty(&ui->search_string) && zsview_is_empty(ui->highlight)) {
    ui->highlight = cstr_zv(&ui->search_string);
    ui_redraw(ui, REDRAW_CURRENT);
  }
}

// don't remove search_string here
void search_nohighlight(Lfm *lfm) {
  if (!zsview_is_empty(lfm->ui.highlight)) {
    lfm->ui.highlight = zsview_init();
    ui_redraw(&lfm->ui, REDRAW_CURRENT);
  }
}

void search(Lfm *lfm, zsview string, bool forward) {
  if (zsview_is_empty(string)) {
    cstr_clear(&lfm->ui.search_string);
    search_nohighlight(lfm);
  } else {
    lfm->ui.search_forward = forward;
    search_highlight(&lfm->ui, string);
  }
}

static void search_next_forward(Lfm *lfm, bool inclusive) {
  if (cstr_is_empty(&lfm->ui.search_string))
    return;

  Dir *dir = fm_current_dir(&lfm->fm);
  search_rehighlight(&lfm->ui);
  for (u32 i = inclusive ? 0 : 1; i < dir_length(dir); i++) {
    u32 idx = (dir->ind + i) % dir_length(dir);
    if (strcasestr(file_name_str(*vec_file_at(&dir->files, idx)),
                   cstr_str(&lfm->ui.search_string))) {
      if (dir_set_cursor(dir, idx, lfm->fm.height, cfg.scrolloff)) {
        ui_on_cursor_moved(&lfm->ui, true);
        ui_redraw(&lfm->ui, REDRAW_CURRENT);
      }
      return;
    }
  }
}

static void search_next_backwards(Lfm *lfm, bool inclusive) {
  if (cstr_is_empty(&lfm->ui.search_string))
    return;

  Dir *dir = fm_current_dir(&lfm->fm);
  search_rehighlight(&lfm->ui);
  for (u32 i = inclusive ? 0 : 1; i < dir_length(dir); i++) {
    u32 idx = (dir->ind + dir_length(dir) - i) % dir_length(dir);
    if (strcasestr(file_name_str(*vec_file_at(&dir->files, idx)),
                   cstr_str(&lfm->ui.search_string))) {
      if (dir_set_cursor(dir, idx, lfm->fm.height, cfg.scrolloff)) {
        ui_on_cursor_moved(&lfm->ui, true);
        ui_redraw(&lfm->ui, REDRAW_CURRENT);
      }
      return;
    }
  }
}

void search_next(Lfm *lfm, bool inclusive) {
  if (lfm->ui.search_forward) {
    search_next_forward(lfm, inclusive);
  } else {
    search_next_backwards(lfm, inclusive);
  }
}

void search_prev(Lfm *lfm, bool inclusive) {
  if (lfm->ui.search_forward) {
    search_next_backwards(lfm, inclusive);
  } else {
    search_next_forward(lfm, inclusive);
  }
}
