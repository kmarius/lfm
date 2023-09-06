#include <stdlib.h>
#include <string.h>

#include "dir.h"
#include "lfm.h"
#include "search.h"

/* pass NULL to highlight previous search */
static inline void search_highlight(Lfm *lfm, const char *string) {
  if (string) {
    xfree(lfm->ui.search_string);
    lfm->ui.search_string = strdup(string);
  }
  lfm->ui.highlight = lfm->ui.search_string;
  ui_redraw(&lfm->ui, REDRAW_FM);
}

// don't remove search_string here
void search_nohighlight(Lfm *lfm) {
  lfm->ui.highlight = NULL;
  ui_redraw(&lfm->ui, REDRAW_FM);
}

void search(Lfm *lfm, const char *string, bool forward) {
  if (!string || *string == 0) {
    XFREE_CLEAR(lfm->ui.search_string);
    search_nohighlight(lfm);
  } else {
    lfm->ui.search_forward = forward;
    search_highlight(lfm, string);
  }
}

static void search_next_forward(Lfm *lfm, bool inclusive) {
  if (!lfm->ui.search_string) {
    return;
  }

  Dir *dir = fm_current_dir(&lfm->fm);
  search_highlight(lfm, NULL);
  for (uint32_t i = inclusive ? 0 : 1; i < dir->length; i++) {
    const uint32_t ind = (dir->ind + i) % dir->length;
    if (strcasestr(file_name(dir->files[ind]), lfm->ui.search_string)) {
      fm_cursor_move_to_ind(&lfm->fm, ind);
      return;
    }
  }
}

static void search_next_backwards(Lfm *lfm, bool inclusive) {
  if (!lfm->ui.search_string) {
    return;
  }

  Dir *dir = fm_current_dir(&lfm->fm);
  search_highlight(lfm, NULL);
  for (uint32_t i = inclusive ? 0 : 1; i < dir->length; i++) {
    const uint32_t ind = (dir->ind - i + dir->length) % dir->length;
    if (strcasestr(file_name(dir->files[ind]), lfm->ui.search_string)) {
      fm_cursor_move_to_ind(&lfm->fm, ind);
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
