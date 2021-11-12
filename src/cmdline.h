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

/*
 * Initialize cmdline.
 */
void cmdline_init(cmdline_t *t);

/*
 * Free resources allocated by cmdline.
 */
void cmdline_deinit(cmdline_t *t);

/*
 * Set the `prefix` of the cmdline.
 */
bool cmdline_prefix_set(cmdline_t *t, const char *prefix);

/*
 * Get the `prefix` of the cmdline. Returns `NULL` if the prefix is empty.
 */
const char *cmdline_prefix_get(cmdline_t *t);

/*
 * Insert the first mb char encountered in key. Returns `true` if a redraw is
 * necessary.
 */
bool cmdline_insert(cmdline_t *t, const char *key);

/*
 * Delete the char left of the cursor. Returns `true` if a redraw is necessary.
 */
bool cmdline_delete(cmdline_t *t);

/*
 * Delete the char right of the cursor. Returns `true` if a redraw is necessary.
 */
bool cmdline_delete_right(cmdline_t *t);

/*
 * Delete the word left of the cursor. Returns `true` if a redraw is necessary.
 */
bool cmdline_delete_word(cmdline_t *t);

/*
 * Delete from the beginning of the line to cursor. Returns `true` if a redraw
 * is necessary.
 */
bool cmdline_delete_line_left(cmdline_t *t);

/*
 * Move the cursor to the left. Returns `true` if a redraw is necessary.
 */
bool cmdline_left(cmdline_t *t);

/*
 * Move the cursor to the right. Returns `true` if a redraw is necessary.
 */
bool cmdline_right(cmdline_t *t);

/*
 * Move the cursor one word left. Returns `true` if a redraw is necessary.
 */
bool cmdline_word_left(cmdline_t *t);

/*
 * Move the cursor one word right. Returns `true` if a redraw is necessary.
 */
bool cmdline_word_right(cmdline_t *t);

/*
 * Move the cursor to the beginning of the line. Returns `true` if a redraw is
 * necessary.
 */
bool cmdline_home(cmdline_t *t);

/*
 * Move the cursor to the end of the line. Returns `true` if a redraw is
 * necessary.
 */
bool cmdline_end(cmdline_t *t);

/*
 * Clear the command line and remove the prefix. Returns `true` if a redraw is
 * necessary.
 */
bool cmdline_clear(cmdline_t *t);

/*
 * If the `prefix` is nonempty, set the text to the left of the cursor. Returns
 * `true` if a redraw is necessary.
 */
bool cmdline_set(cmdline_t *t, const char *line);

/*
 * Set the `prefix` and the command line, placing the cursor between `left` and
 * `right`. Returns `true` if a redraw is necessary.
 */
bool cmdline_set_whole(cmdline_t *t, const char *prefix, const char *left, const char *right);

/*
 * Returns the currend command line without `prefix`.
 */
const char *cmdline_get(cmdline_t *t);

/*
 * Draw the command line into an ncplane.
 */
int cmdline_print(cmdline_t *t, struct ncplane *n);

#endif /* CMDLINE_H */
