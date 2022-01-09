#include <stdlib.h> /* NULL */

char *tokenize(const char *s, char *buf, int *i, int *j)
{
	char c;
	/* skip whitespace */
	while ((c = s[*i]) != 0 && c == ' ') {
		(*i)++;
	}
	if (c == 0) {
		return NULL;
	}
	char *ret = buf + *j;
	while ((c = s[*i]) != 0) {
		(*i)++;
		switch (c) {
			case '"':
				while ((c = s[*i]) != 0 && c != '"') {
					buf[(*j)++] = c;
					(*i)++;
				}
				if (c == 0) {
					/* error: missing '"' */
				} else {
					/* skip closing '"' */
					(*i)++;
				}
				buf[(*j)++] = 0;
				return ret;
				break;
			case ' ':
				buf[(*j)++] = 0;
				return ret;
				break;
			case '\\':
				/* TODO: more escapes here (on 2022-01-09) */
				switch ((c = s[(*i)++])) {
					case ' ':
						buf[(*j)++] = c;
						break;
					default:
						buf[(*j)++] = '\\';
						buf[(*j)++] = c;
				}
				break;
			default:
				buf[(*j)++] = c;
				break;
		}
	}
	buf[(*j)++] = 0;
	return ret;
}
