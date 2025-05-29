#include "keys.h"
#include "config.h"

#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <wctype.h> /* towlower, towlupper */

#define MAX_KEY_NAME_LEN 2 + 9 + 2 + 2 + 2 /* <> + backspace + c- + a- + s- */

static struct {
  uint32_t id;
  const char *name;
} key_names[] = {
    {' ',             "Space"    },
    {'<',             "lt"       },
    {9,               "Tab"      },
    {27,              "Esc"      },
    {NCKEY_INVALID,   "invalid"  },
    {NCKEY_SIGNAL,    "signal"   },
    {NCKEY_UP,        "Up"       },
    {NCKEY_RIGHT,     "Right"    },
    {NCKEY_DOWN,      "Down"     },
    {NCKEY_LEFT,      "Left"     },
    {NCKEY_INS,       "Insert"   },
    {NCKEY_DEL,       "Delete"   },
    {NCKEY_BACKSPACE, "Backspace"},
    {NCKEY_BACKSPACE, "BS"       }, // alias
    {NCKEY_PGDOWN,    "PageDown" },
    {NCKEY_PGUP,      "PageUp"   },
    {NCKEY_HOME,      "Home"     },
    {NCKEY_END,       "End"      },
    {NCKEY_F00,       "F0"       },
    {NCKEY_F01,       "F1"       },
    {NCKEY_F02,       "F2"       },
    {NCKEY_F03,       "F3"       },
    {NCKEY_F04,       "F4"       },
    {NCKEY_F05,       "F5"       },
    {NCKEY_F06,       "F6"       },
    {NCKEY_F07,       "F7"       },
    {NCKEY_F08,       "F8"       },
    {NCKEY_F09,       "F9"       },
    {NCKEY_F01,       "F01"      },
    {NCKEY_F02,       "F02"      },
    {NCKEY_F03,       "F03"      },
    {NCKEY_F04,       "F04"      },
    {NCKEY_F05,       "F05"      },
    {NCKEY_F06,       "F06"      },
    {NCKEY_F07,       "F07"      },
    {NCKEY_F08,       "F08"      },
    {NCKEY_F09,       "F09"      },
    {NCKEY_F10,       "F10"      },
    {NCKEY_F11,       "F11"      },
    {NCKEY_F12,       "F12"      },
    {NCKEY_F13,       "F13"      }, // notcurses seems to map shift/ctrl/alt+f keys to
                        // higher f keys (apparently not in tmux)
    {NCKEY_F14,       "F14"      },
    {NCKEY_F15,       "F15"      },
    {NCKEY_F16,       "F16"      },
    {NCKEY_F17,       "F17"      },
    {NCKEY_F18,       "F18"      },
    {NCKEY_F19,       "F19"      },
    {NCKEY_F20,       "F20"      },
    {NCKEY_F21,       "F21"      },
    {NCKEY_F22,       "F22"      },
    {NCKEY_F23,       "F23"      },
    {NCKEY_F24,       "F24"      },
    {NCKEY_F25,       "F25"      },
    {NCKEY_F26,       "F26"      },
    {NCKEY_F27,       "F27"      },
    {NCKEY_F28,       "F28"      },
    {NCKEY_F29,       "F29"      },
    {NCKEY_F30,       "F30"      },
    {NCKEY_F31,       "F31"      },
    {NCKEY_F32,       "F32"      },
    {NCKEY_F33,       "F33"      },
    {NCKEY_F34,       "F34"      },
    {NCKEY_F35,       "F35"      },
    {NCKEY_F36,       "F36"      },
    {NCKEY_F37,       "F37"      },
    {NCKEY_F38,       "F38"      },
    {NCKEY_F39,       "F39"      },
    {NCKEY_F40,       "F40"      },
    {NCKEY_F41,       "F41"      },
    {NCKEY_F42,       "F42"      },
    {NCKEY_F43,       "F43"      },
    {NCKEY_F44,       "F44"      },
    {NCKEY_F45,       "F45"      },
    {NCKEY_F46,       "F46"      },
    {NCKEY_F47,       "F47"      },
    {NCKEY_F48,       "F48"      },
    {NCKEY_F49,       "F49"      },
    {NCKEY_F50,       "F50"      },
    {NCKEY_F51,       "F51"      },
    {NCKEY_F52,       "F52"      },
    {NCKEY_F53,       "F53"      },
    {NCKEY_F54,       "F54"      },
    {NCKEY_F55,       "F55"      },
    {NCKEY_F56,       "F56"      },
    {NCKEY_F57,       "F57"      },
    {NCKEY_F58,       "F58"      },
    {NCKEY_F59,       "F59"      },
    {NCKEY_F60,       "F60"      },
    {NCKEY_ENTER,     "Enter"    },
    {NCKEY_CLS,       "Clear"    }, // ctrl-l / formfeed?
    {NCKEY_DLEFT,     "DownLeft" },
    {NCKEY_DRIGHT,    "DownRight"},
    {NCKEY_ULEFT,     "UpLeft"   },
    {NCKEY_URIGHT,    "UpRight"  },
    {NCKEY_CENTER,    "Center"   },
    {NCKEY_BEGIN,     "Begin"    },
    {NCKEY_CANCEL,    "Cancel"   },
    {NCKEY_CLOSE,     "Close"    },
    {NCKEY_COMMAND,   "Command"  },
    {NCKEY_COPY,      "Copy"     },
    {NCKEY_EXIT,      "Exit"     },
    {NCKEY_PRINT,     "Print"    },
    {NCKEY_REFRESH,   "Refresh"  }
};

static const int key_names_len = sizeof(key_names) / sizeof(key_names[0]);

const char *input_to_key_name(input_t in, size_t *len_out) {
  static char buf[MAX_KEY_NAME_LEN + 1];
  uint32_t j = 0;
  const char *name = NULL;
  if (ID(in) <= '<' || (ID(in) >= NCKEY_INVALID && ID(in) <= NCKEY_REFRESH)) {
    for (uint32_t i = 0; i < key_names_len; i++) {
      if (key_names[i].id == ID(in)) {
        name = key_names[i].name;
        break;
      }
    }
  }

  bool is_modified = ISSHIFT(in) | ISALT(in) | ISCTRL(in);
  if (is_modified || name != NULL) {
    buf[j++] = '<';
  }

  if (ISSHIFT(in)) {
    buf[j++] = 's';
    buf[j++] = '-';
  }
  if (ISCTRL(in)) {
    buf[j++] = 'c';
    buf[j++] = '-';
  }
  if (ISALT(in)) {
    buf[j++] = 'a';
    buf[j++] = '-';
  }
  if (name != NULL) {
    int len = strlen(name);
    memcpy(buf + j, name, len);
    j += len;
  } else {
    // not a special key
    // check if printable? otherwise notcurses won't print '>'
    int n = wctomb(buf + j, ID(in));
    if (n < 0) {
      buf[j++] = '?';
    } else {
      j += n;
    }
  }
  if (is_modified || name != NULL) {
    buf[j++] = '>';
  }
  buf[j] = 0;
  if (len_out)
    *len_out = j;
  return buf;
}

int key_name_to_input(const char *key, input_t *out) {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;

  // handles "" too
  if (key[0] != '<') {
    return mbtowc((int *)out, key, 8);
  }

  const char *ptr = key + 1;

  if (ptr[0] == 0) {
    // string was "<"
    *out = '<';
    return 1;
  }

  // check modifiers, blatantly checking past nul in a malformed string
  while (ptr[0] != 0 && ptr[1] == '-') {
    char c = tolower(ptr[0]);
    if (c == 'a') {
      if (alt)
        return -1;
      alt = true;
    } else if (c == 'c') {
      if (ctrl)
        return -1;
      ctrl = true;
    } else if (c == 's') {
      if (shift)
        return -1;
      shift = true;
    } else {
      return -1;
    }
    ptr += 2;
  }

  const char *end = strchr(ptr, '>');
  if (end == NULL || end == ptr) {
    return -1;
  }

  // check special key names
  int in = NCKEY_INVALID;
  for (int i = 0; i < key_names_len; i++) {
    if (strncasecmp(ptr, key_names[i].name, end - ptr) == 0) {
      ptr = end + 1;
      in = key_names[i].id;
      break;
    }
  }

  if (in == NCKEY_INVALID) {
    if (strncasecmp(ptr, "leader", end - ptr) == 0) {
      ptr = end + 1;
      in = cfg.mapleader;
    }
  }

  if (in == NCKEY_INVALID) {
    // some other key

    wchar_t w;
    int l = mbtowc(&w, ptr, 8);
    if (l == -1) {
      return -1;
    }

    // notcurses always sends uppercase with ctrl
    in = ctrl ? towupper(w) : (input_t)w;
    ptr += l;

    if (ptr[0] != '>') {
      return -1;
    }

    ptr++;
  }

  if (shift)
    in = SHIFT(in);
  if (ctrl)
    in = CTRL(in);
  if (alt)
    in = ALT(in);

  *out = in;

  return ptr - key;
}

int key_names_to_input(zsview keys, input_t *buf, size_t bufsz) {
  const char *ptr = keys.str;
  const char *end = ptr + keys.size;
  size_t j = 0;

  while (ptr < end) {
    int len = key_name_to_input(ptr, &buf[j++]);
    if (len == -1 || j == bufsz) {
      buf[0] = 0;
      return j < bufsz ? -1 : -2;
    }
    ptr += len;
  }

  buf[j] = 0;
  return 0;
}
