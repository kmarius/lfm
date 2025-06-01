#pragma once

#include "ui.h"

void infoline_init(Ui *ui);

void infoline_parse(zsview infoline);

void infoline_draw(Ui *ui);

int shorten_name(zsview name, char *buf, int max_len, bool has_ext);
