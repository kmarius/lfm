#pragma once

#include "history.h"
#include <notcurses/notcurses.h>
#include <stdbool.h>
#include <wchar.h>

struct vstr {
  char *str;
  size_t cap;
  uint32_t len;
};

struct vwstr {
  wchar_t *str;
  size_t cap;
  uint32_t len;
};

typedef struct cmdline_s {
  struct vstr prefix;
  struct vwstr left;
  struct vwstr right;
  struct vstr buf;
  bool overwrite;
  History history;
} Cmdline;

// Initialize cmdline.
void cmdline_init(Cmdline *t);

// Free resources allocated by cmdline.
void cmdline_deinit(Cmdline *t);

// Set the `prefix` of the cmdline.
bool cmdline_prefix_set(Cmdline *t, const char *prefix);

// Get the `prefix` of the cmdline. Returns `NULL` if the prefix is empty.
const char *cmdline_prefix_get(Cmdline *t);

// Insert the first mb char encountered in key. Returns `true` if a redraw is
// necessary.
bool cmdline_insert(Cmdline *t, const char *key);

// Toggle insert/overwrite.
bool cmdline_toggle_overwrite(Cmdline *t);

// Delete the char left of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete(Cmdline *t);

// Delete the char right of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete_right(Cmdline *t);

// Delete the word left of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete_word(Cmdline *t);

// Delete from the beginning of the line to cursor. Returns `true` if a redraw
// is necessary.
bool cmdline_delete_line_left(Cmdline *t);

// Move the cursor to the left. Returns `true` if a redraw is necessary.
bool cmdline_left(Cmdline *t);

// Move the cursor to the right. Returns `true` if a redraw is necessary.
bool cmdline_right(Cmdline *t);

// Move the cursor one word left. Returns `true` if a redraw is necessary.
bool cmdline_word_left(Cmdline *t);

// Move the cursor one word right. Returns `true` if a redraw is necessary.
bool cmdline_word_right(Cmdline *t);

// Move the cursor to the beginning of the line. Returns `true` if a redraw is
// necessary.
bool cmdline_home(Cmdline *t);

// Move the cursor to the end of the line. Returns `true` if a redraw is
// necessary.
bool cmdline_end(Cmdline *t);

// Clear the command line and remove the prefix. Returns `true` if a redraw is
// necessary.
bool cmdline_clear(Cmdline *t);

// If the `prefix` is nonempty, set the text to the left of the cursor. Returns
// `true` if a redraw is necessary.
bool cmdline_set(Cmdline *t, const char *line);

// Set the command line, placing the cursor between `left` and
// `right`. Returns `true` if a redraw is necessary.
bool cmdline_set_whole(Cmdline *t, const char *left, const char *right);

// Returns the currend command line without `prefix`.
const char *cmdline_get(Cmdline *t);

// Draw the command line into an ncplane. Returns the number of printed
// characters of prefix, left so that the cursor can be positioned.
uint32_t cmdline_print(Cmdline *t, struct ncplane *n);
