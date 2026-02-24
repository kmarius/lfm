#pragma once

// misc utils to work with notcurses and/or stc

#include "stc/cstr.h"
#include "stc/csview.h"

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
const char *ncplane_set_ansi_attrs(struct ncplane *n, const char *s,
                                   const char *end);

static inline int ncplane_putzv(struct ncplane *n, zsview zv) {
  return ncplane_putnstr(n, zv.size, zv.str);
}

static inline int ncplane_putcstr(struct ncplane *n, const cstr *str) {
  return ncplane_putzv(n, cstr_zv(str));
}

// Adds a string to n, interpreting ansi escape sequences and setting the
// attributes to n.
int ncplane_putnstr_ansi_yx(struct ncplane *n, int y, int x, size_t s,
                            const char *gclusters);

static inline int ncplane_putnstr_ansi(struct ncplane *n, size_t s,
                                       const char *gclusters) {
  return ncplane_putnstr_ansi_yx(n, -1, -1, s, gclusters);
}

static inline int ncplane_putstr_ansi_yx(struct ncplane *n, int y, int x,
                                         const char *str) {
  return ncplane_putnstr_ansi_yx(n, y, x, strlen(str), str);
}

static inline int ncplane_putstr_ansi(struct ncplane *n, const char *str) {
  return ncplane_putstr_ansi_yx(n, -1, -1, str);
}

static inline int ncplane_putcs_ansi_yx(struct ncplane *n, int y, int x,
                                        csview cs) {
  return ncplane_putnstr_ansi_yx(n, y, x, cs.size, cs.buf);
}

static inline int ncplane_putcs_ansi(struct ncplane *n, csview cs) {
  return ncplane_putcs_ansi_yx(n, -1, -1, cs);
}

static inline int ncplane_putzv_ansi_yx(struct ncplane *n, int y, int x,
                                        zsview zs) {
  return ncplane_putnstr_ansi_yx(n, y, x, zs.size, zs.str);
}

static inline int ncplane_putzv_ansi(struct ncplane *n, zsview zv) {
  return ncplane_putzv_ansi_yx(n, -1, -1, zv);
}

static inline int ncplane_putcstr_ansi_yx(struct ncplane *n, int y, int x,
                                          const cstr *str) {
  return ncplane_putcs_ansi_yx(n, y, x, cstr_sv(str));
}

static inline int ncplane_putcstr_ansi(struct ncplane *n, const cstr *str) {
  return ncplane_putcstr_ansi_yx(n, -1, -1, str);
}

// stops once printed s cells
int ncplane_putlcs_ansi_yx(struct ncplane *n, int y, int x, size_t s,
                           csview cs);

// stops once printed s cells
static inline int ncplane_putlcstr_ansi_yx(struct ncplane *n, int y, int x,
                                           size_t s, const cstr *str) {
  return ncplane_putlcs_ansi_yx(n, y, x, s, cstr_sv(str));
}

static inline void ncplane_putchar_rep(struct ncplane *n, char c, int rep) {
  for (int i = 0; i < rep; i++) {
    ncplane_putchar(n, c);
  }
}

// Returns the size of the string in wide chars with ansi codes removed.
size_t ansi_mblen(const char *s);
