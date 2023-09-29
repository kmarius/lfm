#pragma once

#include "ui.h"

void statusline_draw(Ui *ui);

static inline void statusline_keyseq_show(Ui *ui, input_t *keyseq) {
  ui->keyseq = keyseq;
  ui_redraw(ui, REDRAW_CMDLINE);
}

static inline void statusline_keyseq_hide(Ui *ui) {
  statusline_keyseq_show(ui, NULL);
}
