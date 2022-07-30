#include <stdlib.h> // NULL

#include "tokenize.h"

char *tokenize(const char *str, char *buf, const char **pos_str, char **pos_buf)
{
  char c;
  if (str) {
    *pos_str = str;
    *pos_buf = buf;
  }

  while ((c = **pos_str) && c == ' ') {
    (*pos_str)++;
  }

  if (!c) {
    return NULL;
  }

  char *ret = *pos_buf;
  while ((c = **pos_str)) {
    (*pos_str)++;
    switch (c) {
      case '"':
        while ((c = **pos_str) && c != '"') {
          *(*pos_buf)++ = c;
          (*pos_str)++;
        }
        if (!c) {
          /* error: missing '"' */
        } else {
          /* skip closing '"' */
          (*pos_str)++;
        }
        *(*pos_buf)++ = 0;
        return ret;
        break;
      case ' ':
        *(*pos_buf)++ = 0;
        return ret;
        break;
      case '\\':
        /* TODO: more escapes here (on 2022-01-09) */
        switch ((c = *(*pos_str)++)) {
          case ' ':
            *(*pos_buf)++ = c;
            break;
          default:
            *(*pos_buf)++ = '\\';
            *(*pos_buf)++ = c;
        }
        break;
      default:
        *(*pos_buf)++ = c;
        break;
    }
  }
  *(*pos_buf)++ = 0;
  return ret;
}
