#include "search.h"

#include "dir.h"
#include "fm.h"
#include "lfm.h"
#include "stcutil.h"
#include "util.h"

/* pass empty to highlight previous search */
static inline void search_highlight(Lfm *lfm, zsview string) {
  if (!zsview_is_empty(string)) {
    cstr_assign_zv(&lfm->ui.search_string, string);
  }
  lfm->ui.highlight = cstr_zv(&lfm->ui.search_string);
  ui_redraw(&lfm->ui, REDRAW_FM);
}

// don't remove search_string here
void search_nohighlight(Lfm *lfm) {
  lfm->ui.highlight = zsview_init();
  ui_redraw(&lfm->ui, REDRAW_FM);
}

void search(Lfm *lfm, zsview string, bool forward) {
  if (zsview_is_empty(string)) {
    cstr_clear(&lfm->ui.search_string);
    search_nohighlight(lfm);
  } else {
    lfm->ui.search_forward = forward;
    search_highlight(lfm, string);
  }
}

static void search_next_forward(Lfm *lfm, bool inclusive) {
  if (cstr_is_empty(&lfm->ui.search_string)) {
    return;
  }

  Dir *dir = fm_current_dir(&lfm->fm);
  search_highlight(lfm, zsview_init());
  for (uint32_t i = inclusive ? 0 : 1; i < dir_length(dir); i++) {
    uint32_t idx = (dir->ind + i) % dir_length(dir);
    if (strcasestr(file_name_str(*vec_file_at(&dir->files, idx)),
                   cstr_str(&lfm->ui.search_string))) {
      fm_cursor_move_to_ind(&lfm->fm, idx);
      ui_update_file_preview(&lfm->ui);
      return;
    }
  }
}

static void search_next_backwards(Lfm *lfm, bool inclusive) {
  if (cstr_is_empty(&lfm->ui.search_string)) {
    return;
  }

  Dir *dir = fm_current_dir(&lfm->fm);
  search_highlight(lfm, zsview_init());
  for (uint32_t i = inclusive ? 0 : 1; i < dir_length(dir); i++) {
    uint32_t idx = (dir->ind + dir_length(dir) - i) % dir_length(dir);
    if (strcasestr(file_name_str(*vec_file_at(&dir->files, idx)),
                   cstr_str(&lfm->ui.search_string))) {
      fm_cursor_move_to_ind(&lfm->fm, idx);
      ui_update_file_preview(&lfm->ui);
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
