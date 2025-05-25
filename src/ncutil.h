#pragma once

#include <notcurses/notcurses.h>

#include "stc/cstr.h"

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
int ncplane_put_str_ansi_yx(struct ncplane *n, int y, int x, const char *s);

static inline int ncplane_put_str_ansi(struct ncplane *n, const char *s) {
  return ncplane_put_str_ansi_yx(n, -1, -1, s);
}

int ncplane_putnstr_ansi_yx(struct ncplane *n, int y, int x, size_t s,
                            const char *ptr);

// Adds a cstr to n, interpreting ansi escape sequences and setting the
// attributes to n.
int ncplane_put_cstr_ansi_yx(struct ncplane *n, int y, int x, const cstr s);

static inline int ncplane_put_cstr_ansi(struct ncplane *n, const cstr s) {
  return ncplane_put_cstr_ansi_yx(n, -1, -1, s);
}

static inline void ncplane_putchar_rep(struct ncplane *n, char c, int rep) {
  for (int i = 0; i < rep; i++) {
    ncplane_putchar(n, c);
  }
}

// Returns the size of the string in wide chars with ansi codes removed.
size_t ansi_mblen(const char *s);

int print_shortened_w(struct ncplane *n, const wchar_t *name, int name_len,
                      int max_len, bool has_ext);
