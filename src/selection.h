#pragma once

#include "defs.h"

#include <stdbool.h>

typedef enum paste_mode_e {
  PASTE_MODE_MOVE,
  PASTE_MODE_COPY,
} paste_mode;

struct Fm;
struct Dir;

// Toggle the given path in the selection.
void selection_toggle_path(struct Fm *fm, zsview path, bool run_hook);

// Add `path` to the current selection if not already contained.
void selection_add_path(struct Fm *fm, zsview path, bool run_hook);

// Clear the selection completely.
bool selection_clear(struct Fm *fm);

// Reverse the file selection in the given directory.
void selection_reverse(struct Fm *fm, struct Dir *dir);

// Write the current celection to the file given as `path`.
// Directories are created as needed.
void selection_write(struct Fm *fm, zsview path);

// Set the current selection into the load buffer with mode `mode`.
void paste_mode_set(struct Fm *fm, paste_mode mode);

// Get the mode current load, one of `MODE_COPY`, `MODE_MOVE`.
paste_mode paste_mode_get(const struct Fm *fm);

// Clear copy/move buffer. Returns the size of the buffer bofore clearing.
bool paste_buffer_clear(struct Fm *fm);

// Add a path to the paste buffer.
void paste_buffer_add(struct Fm *fm, zsview path);
