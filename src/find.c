#include <stdbool.h>
#include <stdint.h>

#include "fm.h"
#include "lfm.h"
#include "ui.h"

bool find(Fm *fm, const char *prefix)
{
  free(fm->find_prefix);
  fm->find_prefix = strdup(prefix);

  Dir *dir = fm_current_dir(fm);
  uint32_t nmatches = 0;
  uint32_t first_match;
  for (uint32_t i = 0; i < dir->length; i++) {
    const uint32_t ind = (dir->ind + i) % dir->length;
    if (hascaseprefix(file_name(dir->files[ind]), prefix)) {
      if (++nmatches == 1) {
        first_match = ind;
      } else {
        break;
      }
    }
  }
  if (nmatches > 0) {
    fm_cursor_move_to_ind(fm, first_match);
    ui_redraw(&fm->lfm->ui, REDRAW_FM);
  }
  return nmatches == 1;
}


void find_next(Fm *fm)
{
  if (!fm->find_prefix) {
    return;
  }

  Dir *dir = fm_current_dir(fm);
  for (uint32_t i = 0; i < dir->length; i++) {
    const uint32_t ind = (dir->ind + 1 + i) % dir->length;
    if (hascaseprefix(file_name(dir->files[ind]), fm->find_prefix)) {
      fm_cursor_move_to_ind(fm, ind);
      ui_redraw(&fm->lfm->ui, REDRAW_FM);
      return;
    }
  }
}


void find_prev(Fm *fm)
{
  if (!fm->find_prefix) {
    return;
  }

  Dir *dir = fm_current_dir(fm);
  for (uint32_t i = 0; i < dir->length; i++) {
    const uint32_t ind = (dir->ind - 1 - i + dir->length) % dir->length;
    if (hascaseprefix(file_name(dir->files[ind]), fm->find_prefix)) {
      fm_cursor_move_to_ind(fm, ind);
      ui_redraw(&fm->lfm->ui, REDRAW_FM);
      return;
    }
  }
}


void find_clear(Fm *fm)
{
  free(fm->find_prefix);
  fm->find_prefix = NULL;
}
