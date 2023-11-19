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

typedef struct Cmdline {
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

// Insert the first multibyte char encountered in `key`. Returns `true` if a
// redraw is necessary.
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

// Set the command line. `left` and `right` are the strings left and right of
// the cursor, respectively. NULL is accepted.
bool cmdline_set(Cmdline *t, const char *left, const char *right);

// Returns the currend command line.
const char *cmdline_get(Cmdline *t);

// Draw the command line into an ncplane. Returns the number of printed
// characters of prefix, left so that the cursor can be positioned.
uint32_t cmdline_draw(Cmdline *t, struct ncplane *n);
