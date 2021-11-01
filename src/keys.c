#include <ncurses.h>
#include <notcurses/notcurses.h>
#include <wchar.h>

#include "app.h" /* error() */
#include "keys.h"
#include "util.h"

/* KEY_DL          Delete line */
/* KEY_IL          Insert line */
/* KEY_IC          Insert char or enter insert mode */
/* KEY_EIC         Exit insert char mode */
/* KEY_CLEAR       Clear screen */
/* KEY_EOS         Clear to end of screen */
/* KEY_EOL         Clear to end of line */
/* KEY_SF          Scroll 1 line forward */
/* KEY_SR          Scroll 1 line backward (reverse) */
/* KEY_NPAGE       Next page */
/* KEY_PPAGE       Previous page */
/* KEY_STAB        Set tab */
/* KEY_CTAB        Clear tab */
/* KEY_CATAB       Clear all tabs */
/* case KEY_ENTER:  #<{(| not enter? |)}># */
/* 	return "<enter>"; */
/* KEY_SRESET      Soft (partial) reset */
/* KEY_RESET       Reset or hard reset */
/* KEY_PRINT       Print or copy */
/* KEY_LL          Home down or bottom (lower left) */
/* KEY_A1          Upper left of keypad */
/* KEY_A3          Upper right of keypad */
/* KEY_B2          Center of keypad */
/* KEY_C1          Lower left of keypad */
/* KEY_C3          Lower right of keypad */
/* KEY_BEG         Beg(inning) key */
/* KEY_CANCEL      Cancel key */
/* KEY_CLOSE       Close key */
/* KEY_COMMAND     Cmd (command) key */
/* KEY_COPY        Copy key */
/* KEY_CREATE      Create key */
/* KEY_EXIT        Exit key */
/* KEY_FIND        Find key */
/* KEY_HELP        Help key */
/* KEY_MARK        Mark key */
/* KEY_MESSAGE     Message key */
/* KEY_MOUSE       Mouse event read */
/* KEY_MOVE        Move key */
/* KEY_NEXT        Next object key */
/* KEY_OPEN        Open key */
/* KEY_OPTIONS     Options key */
/* KEY_PREVIOUS    Previous object key */
/* KEY_REDO        Redo key */
/* KEY_REFERENCE   Ref(erence) key */
/* KEY_REFRESH     Refresh key */
/* KEY_REPLACE     Replace key */
/* KEY_RESIZE      Screen resized */
/* KEY_RESTART     Restart key */
/* KEY_RESUME      Resume key */
/* KEY_SAVE        Save key */
/* KEY_SBEG        Shifted beginning key */
/* KEY_SCANCEL     Shifted cancel key */
/* KEY_SCOMMAND    Shifted command key */
/* KEY_SCOPY       Shifted copy key */
/* KEY_SCREATE     Shifted create key */
/* KEY_SDC         Shifted delete char key */
/* KEY_SDL         Shifted delete line key */
/* KEY_SELECT      Select key */
/* KEY_SEND        Shifted end key */
/* KEY_SEOL        Shifted clear line key */
/* KEY_SEXIT       Shifted exit key */
/* KEY_SFIND       Shifted find key */
/* KEY_SHELP       Shifted help key */
/* KEY_SHOME       Shifted home key */
/* KEY_SIC         Shifted input key */
/* KEY_SLEFT       Shifted left arrow key */
/* KEY_SMESSAGE    Shifted message key */
/* KEY_SMOVE       Shifted move key */
/* KEY_SNEXT       Shifted next key */
/* KEY_SOPTIONS    Shifted options key */
/* KEY_SPREVIOUS   Shifted prev key */
/* KEY_SPRINT      Shifted print key */
/* KEY_SREDO       Shifted redo key */
/* KEY_SREPLACE    Shifted replace key */
/* KEY_SRIGHT      Shifted right arrow */
/* KEY_SRSUME      Shifted resume key */
/* KEY_SSAVE       Shifted save key */
/* KEY_SSUSPEND    Shifted suspend key */
/* KEY_SUNDO       Shifted undo key */
/* KEY_SUSPEND     Suspend key */
/* KEY_UNDO        Undo key */

const char *keytrans(int key)
{
	static char buf[8]; /* hope that fits */
	switch (key) {
	case KEY_BREAK:
		return "<break>";
	case KEY_DOWN:
	case NCKEY_DOWN:
		return "<down>";
	case KEY_UP:
	case NCKEY_UP:
		return "<up>";
	case KEY_LEFT:
	case NCKEY_LEFT:
		return "<left>";
	case KEY_RIGHT:
	case NCKEY_RIGHT:
		return "<right>";
	case KEY_HOME:
	case NCKEY_HOME:
		return "<home>";
	case KEY_BACKSPACE:
	case NCKEY_BACKSPACE:
		return "<backspace>";
	case 127: /* tmux sends this instead */
		return "<backspace>";
	case KEY_F(1):
		return "<f-1>";
	case KEY_F(2):
		return "<f-2>";
	case KEY_F(3):
		return "<f-3>";
	case KEY_F(4):
		return "<f-4>";
	case KEY_F(5):
		return "<f-5>";
	case KEY_F(6):
		return "<f-6>";
	case KEY_F(7):
		return "<f-7>";
	case KEY_F(8):
		return "<f-8>";
	case KEY_F(9):
		return "<f-9>";
	case KEY_F(10):
		return "<f-10>";
	case KEY_F(11):
		return "<f-11>";
	case KEY_F(12):
		return "<f-12>";
	case KEY_DC:
	case NCKEY_DEL:
		return "<delete>";
	case NCKEY_END:
	case KEY_END:
		return "<end>";
	case CTRL('a'):
		return "<c-a>";
	case CTRL('b'):
		return "<c-a>";
	case CTRL('c'):
		return "<c-c>";
	case CTRL('d'):
		return "<c-d>";
	case CTRL('e'):
		return "<c-e>";
	case CTRL('f'):
		return "<c-f>";
	case CTRL('g'):
		return "<c-g>";
	case CTRL('h'):
		return "<c-h>";
	case CTRL('i'):
		return "<tab>";
	case KEY_BTAB:
		return "<backtab>";
	case CTRL('j'):
		return "<c-j>";
	case CTRL('k'):
		return "<c-k>";
	case CTRL('l'):
		return "<c-l>";
	case NCKEY_ENTER:
	/* case CTRL('m'): */
		return "<enter>";
	case CTRL('n'):
		return "<c-n>";
	case CTRL('o'):
		return "<c-o>";
	case CTRL('p'):
		return "<c-p>";
	case CTRL('q'):
		return "<c-q>";
	case CTRL('r'):
		return "<c-r>";
	case CTRL('s'):
		return "<c-s>";
	case CTRL('t'):
		return "<c-t>";
	case CTRL('u'):
		return "<c-u>";
	case CTRL('v'):
		return "<c-v>";
	case CTRL('w'):
		return "<c-w>";
	case CTRL('x'):
		return "<c-x>";
	case CTRL('y'):
		return "<c-y>";
	case CTRL('z'):
		return "<c-z>";
	case ALT('a'):
		return "<a-a>";
	case ALT('b'):
		return "<a-a>";
	case ALT('c'):
		return "<a-c>";
	case ALT('d'):
		return "<a-d>";
	case ALT('e'):
		return "<a-e>";
	case ALT('f'):
		return "<a-f>";
	case ALT('g'):
		return "<a-g>";
	case ALT('h'):
		return "<a-h>";
	case ALT('i'):
		return "<a-i>";
	case ALT('j'):
		return "<a-j>";
	case ALT('k'):
		return "<a-k>";
	case ALT('l'):
		return "<a-l>";
	case ALT('m'):
		return "<enter>";
	case ALT('n'):
		return "<a-n>";
	case ALT('o'):
		return "<a-o>";
	case ALT('p'):
		return "<a-p>";
	case ALT('q'):
		return "<a-q>";
	case ALT('r'):
		return "<a-r>";
	case ALT('s'):
		return "<a-s>";
	case ALT('t'):
		return "<a-t>";
	case ALT('u'):
		return "<a-u>";
	case ALT('v'):
		return "<a-v>";
	case ALT('w'):
		return "<a-w>";
	case ALT('x'):
		return "<a-x>";
	case ALT('y'):
		return "<a-y>";
	case ALT('z'):
		return "<a-z>";
	case ALT('0'):
		return "<a-0>";
	case ALT('1'):
		return "<a-1>";
	case ALT('2'):
		return "<a-2>";
	case ALT('3'):
		return "<a-3>";
	case ALT('4'):
		return "<a-4>";
	case ALT('5'):
		return "<a-5>";
	case ALT('6'):
		return "<a-6>";
	case ALT('7'):
		return "<a-7>";
	case ALT('8'):
		return "<a-8>";
	case ALT('9'):
		return "<a-9>";
	case 27:
		return "<esc>";
	case ' ':
		return "<space>";
	default:
		{
			int n = wctomb(buf, key);
			if (n < 0) {
				// invalid character
				n = 0;
			}
			buf[n] = '\0';
			return buf;
		}
	}
}

int keytrans_inv(char *key)
{
	(void) key;
	return 0;
}

int *keytrans_inv_str(const char *keys, int *buf)
{
	wchar_t w;
	const char *c = keys;
	int l, i = 0;

	while (*c) {
		if (*c == '<') {
			c++;
			if (hasprefix(c, "a-")) {
				c += 2;
				l = mbtowc(&w, c, 10);
				if (l == -1) {
					// unrecognized key
					break;
				}
				buf[i++] = ALT(w);
				c += l + 1;
			} else if (hasprefix(c, "c-")) {
				c += 2;
				l = mbtowc(&w, c, 10);
				if (l == -1) {
					// unrecognized key
					break;
				}
				buf[i++] = CTRL(w);
				c += l + 1;
			} else if (hasprefix(c, "f-")) {
				c += 2;
				int n = *c - '0';
				c++;
				if (*c != '>') {
					n *= 10;
					n += *c - '0';
					c++;
				}
				c++;
			} else if (hasprefix(c, "backspace>")) {
				buf[i++] = NCKEY_BACKSPACE;
				c += 10;
			} else if (hasprefix(c, "backtab>")) {
				buf[i++] = KEY_BTAB;
				c += 8;
			} else if (hasprefix(c, "break>")) {
				buf[i++] = KEY_BREAK;
				c += 6;
			} else if (hasprefix(c, "delete>")) {
				buf[i++] = NCKEY_DEL;
				c += 7;
			} else if (hasprefix(c, "down>")) {
				buf[i++] = NCKEY_DOWN;
				c += 5;
			} else if (hasprefix(c, "end>")) {
				buf[i++] = NCKEY_END;
				c += 4;
			} else if (hasprefix(c, "enter>")) {
				buf[i++] = NCKEY_ENTER;
				c += 6;
			} else if (hasprefix(c, "esc>")) {
				buf[i++] = 27;
				c += 4;
			} else if (hasprefix(c, "home>")) {
				buf[i++] = NCKEY_HOME;
				c += 5;
			} else if (hasprefix(c, "left>")) {
				buf[i++] = NCKEY_LEFT;
				c += 5;
			} else if (hasprefix(c, "lt>")) {
				buf[i++] = '<';
				c += 3;
			} else if (hasprefix(c, "right>")) {
				buf[i++] = NCKEY_RIGHT;
				c += 6;
			} else if (hasprefix(c, "space>")) {
				buf[i++] = ' ';
				c += 6;
			} else if (hasprefix(c, "tab>")) {
				buf[i++] = CTRL('i');
				c += 4;
			} else if (hasprefix(c, "up>")) {
				buf[i++] = NCKEY_UP;
				c += 3;
			} else {
				error("unrecognized key: %s", c);
				break;
			}
		} else {
			l = mbtowc(buf + i, c, 8);
			if (l == -1) {
				// unrecognized key
				break;
			}
			i++; c += l;
		}
	}

	buf[i] = 0;
	return buf;
}
