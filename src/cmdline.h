#ifndef CMDLINE_H
#define CMDLINE_H

#include <notcurses/notcurses.h>
#include <stdbool.h>
#include <wchar.h>

struct vstr {
	char *str;
	size_t cap;
	int len;
};

struct vwstr {
	wchar_t *str;
	size_t cap;
	int len;
};

typedef struct cmdline_t {
	struct vstr prefix;
	struct vwstr left;
	struct vwstr right;
	struct vstr buf;
} cmdline_t;

void cmdline_init(cmdline_t *t);
void cmdline_deinit(cmdline_t *t);
bool cmdline_prefix_set(cmdline_t *t, const char *prefix);
const char *cmdline_prefix_get(cmdline_t *t);
bool cmdline_insert(cmdline_t *t, const char *key);
bool cmdline_delete(cmdline_t *t);
bool cmdline_delete_right(cmdline_t *t);
bool cmdline_delete_word(cmdline_t *t);
bool cmdline_left(cmdline_t *t);
bool cmdline_right(cmdline_t *t);
bool cmdline_word_left(cmdline_t *t);
bool cmdline_word_right(cmdline_t *t);
bool cmdline_home(cmdline_t *t);
bool cmdline_end(cmdline_t *t);
bool cmdline_clear(cmdline_t *t);
bool cmdline_set(cmdline_t *t, const char *line);
const char *cmdline_get(cmdline_t *t);
int cmdline_print(cmdline_t *t, struct ncplane *n);

#endif /* CMDLINE_H */
