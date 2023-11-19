#include "memory.h"

#include <assert.h>
#include <string.h>

// strchrsub and xstrlcpy are borrowed from neovim

/// Replaces every instance of `c` with `x`.
///
/// @warning Will read past `str + strlen(str)` if `c == NUL`.
///
/// @param str A NUL-terminated string.
/// @param c   The unwanted byte.
/// @param x   The replacement.
void strchrsub(char *str, char c, char x) {
  assert(c != '\0');
  while ((str = strchr(str, c))) {
    *str++ = x;
  }
}

/// xstrlcpy - Copy a NUL-terminated string into a sized buffer
///
/// Compatible with *BSD strlcpy: the result is always a valid NUL-terminated
/// string that fits in the buffer (unless, of course, the buffer size is
/// zero). It does not pad out the result like strncpy() does.
///
/// @param[out]  dst  Buffer to store the result.
/// @param[in]  src  String to be copied.
/// @param[in]  dsize  Size of `dst`.
///
/// @return Length of `src`. May be greater than `dsize - 1`, which would mean
///         that string was truncated.
size_t xstrlcpy(char *restrict dst, const char *restrict src, size_t dsize) {
  size_t slen = strlen(src);

  if (dsize) {
    size_t len = slen < dsize - 1 ? slen : dsize - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
  }

  return slen; // Does not include NUL.
}
