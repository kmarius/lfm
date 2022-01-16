#include <stdint.h>
#include <stdlib.h> // NULL

#include "tokenize.h"

char *tokenize(const char *s, char *buf, uint16_t *i, uint16_t *j)
{
	char c;

	while ((c = s[*i]) && c == ' ')
		(*i)++;

	if (!c)
		return NULL;

	char *ret = buf + *j;
	while ((c = s[*i])) {
		(*i)++;
		switch (c) {
			case '"':
				while ((c = s[*i]) && c != '"') {
					buf[(*j)++] = c;
					(*i)++;
				}
				if (!c) {
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
