#pragma once

#include "history.h"

#include <notcurses/notcurses.h>

#include <stdbool.h>

typedef struct Cmdline {
  cstr prefix;
  cstr left;
  cstr right;
  cstr buf;
  bool overwrite;
  History history;
} Cmdline;

#define Self Cmdline

// Initialize cmdline.
void cmdline_init(Self *self);

// Free resources allocated by cmdline.
void cmdline_deinit(Self *self);

// Set the `prefix` of the cmdline.
bool cmdline_prefix_set(Self *self, zsview zv);

// Insert the first multibyte char encountered in `key`. Returns `true` if a
// redraw is necessary.
bool cmdline_insert(Self *self, zsview zv);

// Toggle insert/overwrite.
bool cmdline_toggle_overwrite(Self *self);

// Delete the char left of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete(Self *self);

// Delete the char right of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete_right(Self *self);

// Delete the word left of the cursor. Returns `true` if a redraw is necessary.
bool cmdline_delete_word(Self *self);

// Delete from the beginning of the line to cursor. Returns `true` if a redraw
// is necessary.
bool cmdline_delete_line_left(Self *self);

// Move the cursor to the left. Returns `true` if a redraw is necessary.
bool cmdline_left(Self *self);

// Move the cursor to the right. Returns `true` if a redraw is necessary.
bool cmdline_right(Self *self);

// Move the cursor one word left. Returns `true` if a redraw is necessary.
bool cmdline_word_left(Self *self);

// Move the cursor one word right. Returns `true` if a redraw is necessary.
bool cmdline_word_right(Self *self);

// Move the cursor to the beginning of the line. Returns `true` if a redraw is
// necessary.
bool cmdline_home(Self *self);

// Move the cursor to the end of the line. Returns `true` if a redraw is
// necessary.
bool cmdline_end(Self *self);

// Clear the command line and remove the prefix. Returns `true` if a redraw is
// necessary.
bool cmdline_clear(Self *self);

// Set the command line. `left` and `right` are the strings left and right of
// the cursor, respectively.
bool cmdline_set(Self *self, zsview left, zsview right);

// Returns the current, complete command line.
zsview cmdline_get(Self *self);

// Draw the command line into an ncplane. Returns the number of printed
// characters of prefix, left so that the cursor can be positioned.
int cmdline_draw(Self *self, struct ncplane *n);

#undef Self
