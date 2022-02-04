#include <wchar.h>
#include <wctype.h> /* towlower, towlupper */

#include "keys.h"
#include "util.h" /* hascaseprefix */

#define MAX_KEY_NAME_LEN 2 + 9 + 2 + 2 + 2 /* <> + backspace + c- + a- + s- */

static struct {
	uint32_t id;
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


static const uint32_t key_names_len = sizeof(key_names_map) / sizeof(key_names_map[0]);


const char *input_to_key_name(input_t in)
{
	static char buf[MAX_KEY_NAME_LEN + 1];
	uint32_t j = 0;
	const char *name = NULL;
	if (ID(in) <= '<' || (ID(in) >= NCKEY_INVALID && ID(in) <= NCKEY_REFRESH)) {
		for (uint32_t i = 0; i < key_names_len; i++) {
			if (key_names_map[i].id == ID(in)) {
				name = key_names_map[i].name;
				break;
			}
		}
	}
	if (ISSHIFT(in)|ISALT(in)|ISCTRL(in) || name)
		buf[j++] = '<';

	if (ISSHIFT(in)) {
		buf[j++] = 'S';
		buf[j++] = '-';
	}
	if (ISCTRL(in)) {
		buf[j++] = 'C';
		buf[j++] = '-';
	}
	if (ISALT(in)) {
		buf[j++] = 'A';
		buf[j++] = '-';
	}
	if (name) {
		strcpy(buf+j, name);
		j += strlen(name);
	} else {
		// not a special key
		// check if printable? otherwise notcurses won't print '>'
		int n = wctomb(buf+j, ID(in));
		if (n < 0)
			buf[j++] = '?';
		else
			j += n;
	}
	if (ISSHIFT(in)|ISALT(in)|ISCTRL(in) || name)
		buf[j++] = '>';
	buf[j++] = 0;
	return buf;
}


input_t *key_names_to_input(const char *keys, input_t *buf)
{
	const char *pos = keys;
	uint32_t j = 0;
	while (*pos != 0) {
		bool shift = false;
		bool ctrl = false;
		bool alt = false;

		if (*pos == '<') {
			pos++;
			if (hascaseprefix(pos, "a-")) {
				alt = true;
				pos += 2;
			}
			if (hascaseprefix(pos, "c-")) {
				ctrl = true;
				pos += 2;
			}
			if (hascaseprefix(pos, "s-")) {
				shift = true;
				pos += 2;
			}

			uint32_t in = NCKEY_INVALID;
			for (uint32_t i = 0; i < key_names_len; i++) {
				if (hascaseprefix(pos, key_names_map[i].name)) {
					in = key_names_map[i].id;
					pos += strlen(key_names_map[i].name);
					break;
				}
			}

			if (in == NCKEY_INVALID) {
				wchar_t w;
				int l = mbtowc(&w, pos, 8);
				if (l == -1)
					break; // unrecognized key

				if (ctrl)
					in = towupper(w);
				else
					in = towlower(w);
				pos += l;
			}

			if (shift)
				in = SHIFT(in);
			if (ctrl)
				in = CTRL(in);
			if (alt)
				in = ALT(in);
			buf[j++] = in;
			pos++;
		} else {
			wchar_t w;
			int l = mbtowc(&w, pos, 8);
			if (l == -1)
				break; // unrecognized key
			buf[j++] = w;
			pos += l;
		}
	}

	buf[j] = 0;
	return buf;
}
