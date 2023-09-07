#pragma once

#include <notcurses/notcurses.h>

#define NCCHANNEL_INITIALIZER_PALINDEX(ind)                                    \
  (ind < 0 ? ~NC_BGDEFAULT_MASK & 0xff000000lu                                 \
           : (((NC_BGDEFAULT_MASK | NC_BG_PALETTE) & 0xff000000lu) |           \
              (ind & 0xff)))

#define NCCHANNEL_INITIALIZER_HEX(hex)                                         \
  (hex < 0 ? ~NC_BGDEFAULT_MASK & 0xff000000lu                                 \
           : ((NC_BGDEFAULT_MASK & 0xff000000lu) | (hex & 0xffffff)))

#define NCCHANNELS_INITIALIZER_PALINDEX(fg, bg)                                \
  ((NCCHANNEL_INITIALIZER_PALINDEX(fg) << 32lu) |                              \
   NCCHANNEL_INITIALIZER_PALINDEX(bg))

// Consumes the ansi escape sequence pointed to by s setting the attributes to
// n. Returns a pointer to the char after the sequence.
const char *ncplane_set_ansi_attrs(struct ncplane *n, const char *s);

// Adds a string to n, interpreting ansi escape sequences and setting the
// attributes to n.
int ncplane_addastr_yx(struct ncplane *n, int y, int x, const char *s);

static inline int ncplane_addastr(struct ncplane *n, const char *s) {
  return ncplane_addastr_yx(n, -1, -1, s);
}

// Returns the size of the string in wide chars with ansi codes removed.
size_t ansi_mblen(const char *s);
