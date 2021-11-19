#include <notcurses/notcurses.h>
#include <wchar.h>
#include <wctype.h> /* towlower, towlupper */

#include "keys.h"
#include "util.h" /* hascaseprefix */

#define MAX_KEY_NAME_LEN 2 + 9 + 2 + 2 + 2 /* <> + backspace + c- + a- + s- */

static struct {
	int id;
	const char *name;
} key_names_map[] = {
	{' ', "Space"},
	{'<', "lt"},
	{9, "Tab"},
	{27, "Esc"},
	{NCKEY_INVALID, "invalid"},
	{NCKEY_SIGNAL , "signal"},
	{NCKEY_UP, "Up"},
	{NCKEY_RIGHT, "Right"},
	{NCKEY_DOWN, "Down"},
	{NCKEY_LEFT, "Left"},
	{NCKEY_INS, "Insert"},
	{NCKEY_DEL, "Delete"},
	{NCKEY_BACKSPACE, "Backspace"},
	{NCKEY_BACKSPACE, "BS"}, // alias
	{NCKEY_PGDOWN, "PageDown"},
	{NCKEY_PGUP, "PageUp"},
	{NCKEY_HOME, "Home"},
	{NCKEY_END, "End"},
	{NCKEY_F00, "F0"},
	{NCKEY_F01, "F1"},
	{NCKEY_F02, "F2"},
	{NCKEY_F03, "F3"},
	{NCKEY_F04, "F4"},
	{NCKEY_F05, "F5"},
	{NCKEY_F06, "F6"},
	{NCKEY_F07, "F7"},
	{NCKEY_F08, "F8"},
	{NCKEY_F09, "F9"},
	{NCKEY_F01, "F01"},
	{NCKEY_F02, "F02"},
	{NCKEY_F03, "F03"},
	{NCKEY_F04, "F04"},
	{NCKEY_F05, "F05"},
	{NCKEY_F06, "F06"},
	{NCKEY_F07, "F07"},
	{NCKEY_F08, "F08"},
	{NCKEY_F09, "F09"},
	{NCKEY_F10, "F10"},
	{NCKEY_F11, "F11"},
	{NCKEY_F12, "F12"},
	{NCKEY_F13, "F13"}, // notcurses seems to map shift/ctrl/alt+f keys to higher f keys (apparently not in tmux)
	{NCKEY_F14, "F14"},
	{NCKEY_F15, "F15"},
	{NCKEY_F16, "F16"},
	{NCKEY_F17, "F17"},
	{NCKEY_F18, "F18"},
	{NCKEY_F19, "F19"},
	{NCKEY_F20, "F20"},
	{NCKEY_F21, "F21"},
	{NCKEY_F22, "F22"},
	{NCKEY_F23, "F23"},
	{NCKEY_F24, "F24"},
	{NCKEY_F25, "F25"},
	{NCKEY_F26, "F26"},
	{NCKEY_F27, "F27"},
	{NCKEY_F28, "F28"},
	{NCKEY_F29, "F29"},
	{NCKEY_F30, "F30"},
	{NCKEY_F31, "F31"},
	{NCKEY_F32, "F32"},
	{NCKEY_F33, "F33"},
	{NCKEY_F34, "F34"},
	{NCKEY_F35, "F35"},
	{NCKEY_F36, "F36"},
	{NCKEY_F37, "F37"},
	{NCKEY_F38, "F38"},
	{NCKEY_F39, "F39"},
	{NCKEY_F40, "F40"},
	{NCKEY_F41, "F41"},
	{NCKEY_F42, "F42"},
	{NCKEY_F43, "F43"},
	{NCKEY_F44, "F44"},
	{NCKEY_F45, "F45"},
	{NCKEY_F46, "F46"},
	{NCKEY_F47, "F47"},
	{NCKEY_F48, "F48"},
	{NCKEY_F49, "F49"},
	{NCKEY_F50, "F50"},
	{NCKEY_F51, "F51"},
	{NCKEY_F52, "F52"},
	{NCKEY_F53, "F53"},
	{NCKEY_F54, "F54"},
	{NCKEY_F55, "F55"},
	{NCKEY_F56, "F56"},
	{NCKEY_F57, "F57"},
	{NCKEY_F58, "F58"},
	{NCKEY_F59, "F59"},
	{NCKEY_F60, "F60"},
	{NCKEY_ENTER, "Enter"},
	{NCKEY_CLS, "Clear"}, // ctrl-l / formfeed?
	{NCKEY_DLEFT, "DownLeft"},
	{NCKEY_DRIGHT, "DownRight"},
	{NCKEY_ULEFT, "UpLeft"},
	{NCKEY_URIGHT, "UpRight"},
	{NCKEY_CENTER, "Center"},
	{NCKEY_BEGIN, "Begin"},
	{NCKEY_CANCEL, "Cancel"},
	{NCKEY_CLOSE, "Close"},
	{NCKEY_COMMAND, "Command"},
	{NCKEY_COPY, "Copy"},
	{NCKEY_EXIT, "Exit"},
	{NCKEY_PRINT, "Print"},
	{NCKEY_REFRESH, "Refresh"}};

static const int key_names_len = sizeof(key_names_map) / sizeof(key_names_map[0]);

const char *long_to_key_name(const long u)
{
	static char buf[MAX_KEY_NAME_LEN + 1];
	int i;
	const char *name = NULL;
	if (KEY(u) <= '<' || (KEY(u) >= NCKEY_INVALID && KEY(u) <= NCKEY_REFRESH)) {
		for (i = 0; i < key_names_len; i++) {
			if (key_names_map[i].id == KEY(u)) {
				name = key_names_map[i].name;
				break;
			}
		}
	}
	int ind = 0;
	if (ISSHIFT(u)|ISALT(u)|ISCTRL(u) || name) {
		buf[ind++] = '<';
	}
	if (ISSHIFT(u)) {
		buf[ind++] = 'S';
		buf[ind++] = '-';
	}
	if (ISCTRL(u)) {
		buf[ind++] = 'C';
		buf[ind++] = '-';
	}
	if (ISALT(u)) {
		buf[ind++] = 'A';
		buf[ind++] = '-';
	}
	if (name) {
		strcpy(buf+ind, name);
		ind += strlen(name);
	} else {
		// not a special key
		// check if printable? otherwise notcurses won't print '>'
		int n = wctomb(buf+ind, KEY(u));
		if (n < 0) {
			buf[ind++] = '?';
		} else {
			ind += n;
		}
	}
	if (ISSHIFT(u)|ISALT(u)|ISCTRL(u) || name) {
		buf[ind++] = '>';
	}
	buf[ind++] = 0;
	return buf;
}

long *key_names_to_longs(const char *keys, long *buf)
{
	wchar_t w;
	int l, i, ind;
	const char *pos;

	bool shift = false;
	bool ctrl = false;
	bool alt = false;

	ind = 0;
	pos = keys;
	while (*pos != 0) {
		if (*pos == '<') {
			pos++;
			if (hascaseprefix(pos, "a-")) {
				pos += 2;
				alt = true;
			}
			if (hascaseprefix(pos, "c-")) {
				pos += 2;
				ctrl = true;
			}
			if (hascaseprefix(pos, "s-")) {
				pos += 2;
				shift = true;
			}

			long u = -1;
			for (i = 0; i < key_names_len; i++) {
				if (hascaseprefix(pos, key_names_map[i].name)) {
					u = key_names_map[i].id;
					break;
				}
			}

			if (u != -1) {
				pos += strlen(key_names_map[i].name);
			} else {
				l = mbtowc(&w, pos, 8);
				if (l == -1) {
					// unrecognized key
					break;
				}
				if (ctrl) {
					u = towupper(w);
				} else {
					u = towlower(w);
				}
				pos += l;
			}
			if (shift) {
				u = SHIFT(u);
			}
			if (ctrl) {
				u = CTRL(u);
			}
			if (alt) {
				u = ALT(u);
			}
			buf[ind++] = u;
			pos++;
		} else {
			l = mbtowc(&w, pos, 8);
			if (l == -1) {
				// unrecognized key
				break;
			}
			buf[ind++] = w;
			pos += l;
		}
	}

	buf[ind] = 0;
	return buf;
}
