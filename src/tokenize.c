#include <stdlib.h>

char *tokenize(const char *s, char *buf, int *i, int *j)
{
	char c;
	/* skip whitespace */
	while ((c = s[*i]) && c == ' ') {
		(*i)++;
	}
	if (c == 0) {
		return NULL;
	}
	char *ret = buf + *j;
	while ((c = s[*i])) {
		(*i)++;
		switch (c) {
		case '"':
			while ((c = s[*i]) && c != '"') {
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
		default:
			buf[(*j)++] = c;
			break;
		}
	}
	buf[(*j)++] = 0;
	return ret;
}
