#ifndef CMDLINE_H
#define CMDLINE_H

#include <notcurses/notcurses.h>
#include <wchar.h>

#define ACC_SIZE 256
#define PREFIX_SIZE 32

typedef struct cmdline_t {
	char prefix[PREFIX_SIZE];
	wchar_t left[ACC_SIZE];
	wchar_t right[ACC_SIZE];
} cmdline_t;

void cmdline_init(cmdline_t *t);
void cmdline_deinit(cmdline_t *t);
int cmdline_prefix_set(cmdline_t *t, const char *prefix);
const char *cmdline_prefix_get(cmdline_t *t);
int cmdline_insert(cmdline_t *t, const char *key);
int cmdline_delete(cmdline_t *t);
int cmdline_delete_right(cmdline_t *t);
int cmdline_left(cmdline_t *t);
int cmdline_right(cmdline_t *t);
int cmdline_home(cmdline_t *t);
int cmdline_end(cmdline_t *t);
int cmdline_clear(cmdline_t *t);
int cmdline_set(cmdline_t *t, const char *line);
const char *cmdline_get(const cmdline_t *t);
int cmdline_print(cmdline_t *t, struct ncplane *n);

#endif /* CMDLINE_H */
