#pragma once

// Tokenizes a string separated by whitespace. Simple quoting works.
// On the first call, str and buf may not be NULL. On each consecutive call
// str must be NULL.
// Parameters: str:    0 terminated char* to tokenize
//             buf:  Buffer of length >= sizeof(s)
//             pos_str, pos_buf: will be initialized on the first call.
// Returns:    char* to the token inside buf
//             NULL if there are no more tokens
char *tokenize(const char *str, char *buf, const char **pos_str, char **pos_buf);
