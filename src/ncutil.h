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

// consumes the ansi escape sequence pointed to by s setting the attributes to n.
// Returns a pointer to the char after the sequence.
const char *ansi_consoom(struct ncplane *n, const char *s);

// Adds a string to n, interpreting ansi escape sequences and setting the
// attributes to n.
void ansi_addstr(struct ncplane *n, const char *s);

// Returns the size of the string in wide chars with ansi codes removed.
size_t ansi_mblen(const char *s);
