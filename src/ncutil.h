#pragma once

// misc utils to work with notcurses and/or stc

#include "defs.h"

#include <notcurses/notcurses.h>
#include <stc/cstr.h>

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

static inline i32 ncplane_putzv(struct ncplane *n, zsview zv) {
  return ncplane_putnstr(n, zv.size, zv.str);
}

static inline i32 ncplane_putcstr(struct ncplane *n, const cstr *str) {
  return ncplane_putzv(n, cstr_zv(str));
}

// Adds a string to n, interpreting ansi escape sequences and setting the
// attributes to n.
i32 ncplane_putnstr_ansi_yx(struct ncplane *n, i32 y, i32 x, usize s,
                            const char *gclusters);

static inline i32 ncplane_putnstr_ansi(struct ncplane *n, usize s,
                                       const char *gclusters) {
  return ncplane_putnstr_ansi_yx(n, -1, -1, s, gclusters);
}

static inline i32 ncplane_putstr_ansi_yx(struct ncplane *n, i32 y, i32 x,
                                         const char *str) {
  return ncplane_putnstr_ansi_yx(n, y, x, strlen(str), str);
}

static inline i32 ncplane_putstr_ansi(struct ncplane *n, const char *str) {
  return ncplane_putstr_ansi_yx(n, -1, -1, str);
}

static inline i32 ncplane_putcs_ansi_yx(struct ncplane *n, i32 y, i32 x,
                                        csview cs) {
  return ncplane_putnstr_ansi_yx(n, y, x, cs.size, cs.buf);
}

static inline i32 ncplane_putcs_ansi(struct ncplane *n, csview cs) {
  return ncplane_putcs_ansi_yx(n, -1, -1, cs);
}

static inline i32 ncplane_putzv_ansi_yx(struct ncplane *n, i32 y, i32 x,
                                        zsview zs) {
  return ncplane_putnstr_ansi_yx(n, y, x, zs.size, zs.str);
}

static inline i32 ncplane_putzv_ansi(struct ncplane *n, zsview zv) {
  return ncplane_putzv_ansi_yx(n, -1, -1, zv);
}

static inline i32 ncplane_putcstr_ansi_yx(struct ncplane *n, i32 y, i32 x,
                                          const cstr *str) {
  return ncplane_putcs_ansi_yx(n, y, x, cstr_sv(str));
}

static inline i32 ncplane_putcstr_ansi(struct ncplane *n, const cstr *str) {
  return ncplane_putcstr_ansi_yx(n, -1, -1, str);
}

// stops once printed s cells
i32 ncplane_putlcs_ansi_yx(struct ncplane *n, i32 y, i32 x, usize s, csview cs);

// stops once printed s cells
static inline i32 ncplane_putlcstr_ansi_yx(struct ncplane *n, i32 y, i32 x,
                                           usize s, const cstr *str) {
  return ncplane_putlcs_ansi_yx(n, y, x, s, cstr_sv(str));
}

static inline void ncplane_putchar_rep(struct ncplane *n, char c, i32 rep) {
  for (i32 i = 0; i < rep; i++) {
    ncplane_putchar(n, c);
  }
}

// Returns the size of the string in wide chars with ansi codes removed.
usize ansi_mblen(const char *ptr);

// This function prints a ? if it comes across a character it can't print
static inline int ncplane_putstr_sanitized_yx(struct ncplane *n, int y, int x,
                                              const char *gclusters) {
  int ret = 0;
  while (*gclusters) {
    size_t wcs;
    int cols = ncplane_putegc_yx(n, y, x, gclusters, &wcs);
    if (unlikely(cols < 0)) {
      // possible broken char, try to print a ?
      cols = ncplane_putegc_yx(n, y, x, "?", &wcs);
      if (cols < 0)
        return -ret;
    }
    if (wcs == 0)
      break;
    y = -1;
    x = -1;
    gclusters += wcs;
    ret += cols;
  }
  return ret;
}

// This function prints a ? if it comes across a character it can't print
static inline int ncplane_putstr_sanitized(struct ncplane *n,
                                           const char *gclusters) {
  return ncplane_putstr_sanitized_yx(n, -1, -1, gclusters);
}
