#ifndef TOKENIZE_H
#define TOKENIZE_H

/* Tokenizes a string separated by whitespace. Simple quoting works.
 * Parameters: s:    0 terminated char* to tokenize
 *             buf:  Buffer of length >= sizeof(s)
 *             i, j: Should both be 0 on the first call and not changed.
 * Returns:    char* to the token inside buf
 *             NULL if there are no more tokens
 *
 * example: tokenize("abc d \"ef g\"  ", buf, &i, &j) returns
 *          "abc", "d", "ef g", NULL on 4 consecutive calls */
char *tokenize(const char *s, char *buf, int *i, int *j);

#endif
