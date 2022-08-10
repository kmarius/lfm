#pragma once

#include <notcurses/notcurses.h>

const char *ansi_consoom(struct ncplane *w, const char *s);

void ansi_addstr(struct ncplane *n, const char *s);
