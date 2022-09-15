#pragma once

#include <notcurses/notcurses.h>

#define NCCHANNEL_INITIALIZER_PALINDEX(ind) \
  (ind < 0 \
   ? ~NC_BGDEFAULT_MASK & 0xff000000lu \
   : (((NC_BGDEFAULT_MASK | NC_BG_PALETTE) & 0xff000000lu) | (ind & 0xff)))

#define NCCHANNEL_INITIALIZER_HEX(hex) \
  (hex < 0 \
   ? ~NC_BGDEFAULT_MASK & 0xff000000lu \
   : ((NC_BGDEFAULT_MASK & 0xff000000lu) | (hex & 0xffffff)))

#define NCCHANNELS_INITIALIZER_PALINDEX(fg, bg) \
  ((NCCHANNEL_INITIALIZER_PALINDEX(fg) << 32lu) \
   | NCCHANNEL_INITIALIZER_PALINDEX(bg))

const char *ansi_consoom(struct ncplane *w, const char *s);

void ansi_addstr(struct ncplane *n, const char *s);
