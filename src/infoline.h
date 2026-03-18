#pragma once

#include "stc/zsview.h"

struct Ui;

void infoline_init();

// needs to be called after suspending the ui
void infoline_suspend();

void infoline_parse(zsview infoline);

void infoline_draw(struct Ui *ui);
