#pragma once

#include "dir.h"
#include "file.h"
#include "pathlist.h"
#include "stc/types.h"

#include <ev.h>

#include <stdbool.h>
#include <stdint.h>

declare_vec(vec_dir, Dir *);
struct lfm_opts;

typedef enum paste_mode_e {
  PASTE_MODE_MOVE,
  PASTE_MODE_COPY,
} paste_mode;

typedef struct Fm {

  // Height of the fm.
  uint32_t height;

  // Not guaranteed to coincide with PWD or this processe's working directory,
  // which is only set once we know the destination is reachable.
  cstr pwd;

  struct {
    // Visible directories excluding preview. Index 0 is the current directory,
    // all following elements are the parents
    vec_dir visible;
    int max_visible;

    // preview directory, NULL if there is none, e.g. if the cursor is resting
    // on a file.
    Dir *preview;
  } dirs;

  struct {
    // Current selection
    pathlist current;

    // selection that was active when entering visual mode
    pathlist keep_in_visual;

    // Previous selection that can be restored
    pathlist previous;
  } selection;

  struct {
    // Copy/move buffer, hash table of paths.
    pathlist buffer;

    paste_mode mode;
  } paste;

  struct {
    // Is visual selection mode active?
    bool active;

    // Start index of the visual selection.
    uint32_t anchor;
  } visual;

  // timer for delayed preview loading
  ev_timer cursor_resting_timer;

  // Previous directory, not changed on updir/open.
  cstr automark;
} Fm;

// Moves to the correct starting directory, loads initial dirs and sets up
// previews and inotify watchers.
void fm_init(Fm *fm, struct lfm_opts *opts);

// Unloads directories and frees all resources.
void fm_deinit(Fm *fm);

static inline const cstr *fm_getpwd(const Fm *fm) {
  return &fm->pwd;
}

static inline const char *fm_getpwd_str(const Fm *fm) {
  return cstr_str(&fm->pwd);
}

// Updates the number of loaded directories, called after the number of columns
// changes.
void fm_recol(Fm *fm);

// Current directory. Never `NULL`.
#define fm_current_dir(fm) (fm)->dirs.visible.data[0]

// Current file of the current directory. Can be `NULL`.
#define fm_current_file(fm) dir_current_file(fm_current_dir(fm))

// Move the cursor relative to the current position.
bool fm_cursor_move(Fm *fm, int32_t ct);

static inline void fm_cursor_move_to_ind(Fm *fm, uint32_t ind) {
  fm_cursor_move(fm, ind - fm_current_dir(fm)->ind);
}

// Move cursor `ct` up in the current directory.
static inline bool fm_up(Fm *fm, int32_t ct) {
  return fm_cursor_move(fm, -ct);
}

// Move cursor `ct` down in the current directory.
static inline bool fm_down(Fm *fm, int32_t ct) {
  return fm_cursor_move(fm, ct);
}

// Move cursor to the top of the current directory.
static inline bool fm_top(Fm *fm) {
  return fm_up(fm, fm_current_dir(fm)->ind);
}

// Move cursor to the bottom of the current directory.
static inline bool fm_bot(Fm *fm) {
  return fm_down(fm, fm_current_dir(fm)->length - fm_current_dir(fm)->ind);
}

// Scroll up the directory while keeping the cursor position if possible.
bool fm_scroll_up(Fm *fm);

// Scroll down the directory while keeping the cursor position if possible.
bool fm_scroll_down(Fm *fm);

// Changes directory to the directory given by `path`. If `save` then the
// current directory will be saved as the special "'" automark. Returns `true´
// if the directory has been changed.
bool fm_async_chdir(Fm *fm, zsview path, bool save, bool hook);

bool fm_sync_chdir(Fm *fm, zsview path, bool save, bool hook);

// Open the currently selected file: if it is a directory, chdir into it.
// Otherwise return the file so that the caller can open it.
File *fm_open(Fm *fm);

// Chdir to the parent of the current directory.
bool fm_updir(Fm *fm);

// Move the cursor the file with `name` if it exists. Otherwise leaves the
// cursor at the closest valid position (i.e. after the number of files
// decreases)
void fm_move_cursor_to(Fm *fm, zsview name);

void fm_move_cursor_to_ptr(Fm *fm, const File *file);

// Apply the filter string given by `filter` to the current directory.
void fm_filter(Fm *fm, Filter *filter);

// Return the filter string of the currently selected directory.
static inline zsview fm_filter_get(const Fm *fm) {
  return filter_string(fm_current_dir(fm)->filter);
}

//  Show hidden files.
void fm_hidden_set(Fm *fm, bool hidden);

// Checks all visible directories for changes on disk and schedules reloads if
// necessary.
void fm_check_dirs(const Fm *fm);

// jump to previous directory
static inline bool fm_jump_automark(Fm *fm) {
  if (cstr_is_empty(&fm->automark)) {
    return false;
  }
  return fm_async_chdir(fm, cstr_zv(&fm->automark), true, true);
}

// Begin visual selection mode.
void fm_on_visual_enter(Fm *fm);

// End visual selection mode.
void fm_on_visual_exit(Fm *fm);

// Toggles the selection of the currently selected file.
void fm_selection_toggle_current(Fm *fm);

// Add `path` to the current selection if not already contained.
void fm_selection_add(Fm *fm, const cstr *path, bool run_hook);

// Clear the selection completely.
void fm_selection_clear(Fm *fm);

// Reverse the file selection.
void fm_selection_reverse(Fm *fm);

// Write the current celection to the file given as `path`.
// Directories are created as needed.
void fm_selection_write(const Fm *fm, zsview path);

// Set the current selection into the load buffer with mode `mode`.
void fm_paste_mode_set(Fm *fm, paste_mode mode);

// Clear copy/move buffer. Returns the size of the buffer bofore clearing.
static inline bool fm_paste_buffer_clear(Fm *fm) {
  // TODO
  size_t prev_size = pathlist_size(&fm->paste.buffer);
  if (prev_size > 0) {
    // swap paste_buffer and prev
    pathlist tmp = fm->selection.previous;
    fm->selection.previous = fm->paste.buffer;
    fm->paste.buffer = tmp;
    pathlist_clear(&fm->paste.buffer);
  }
  return prev_size;
}

// Add a path to the paste buffer.
static inline void fm_paste_buffer_add(Fm *fm, const cstr *file) {
  pathlist_add(&fm->paste.buffer, file);
}

// Get the mode current load, one of `MODE_COPY`, `MODE_MOVE`.
static inline paste_mode fm_paste_mode_get(const Fm *fm) {
  return fm->paste.mode;
}

// Drop directory cache and reload visible directories from disk.
void fm_drop_cache(Fm *fm);

// Reload visible directories.
void fm_reload(Fm *fm);

// Update preview (e.g. after moving the cursor).
void fm_update_preview(Fm *fm);

// Flatten the current directory up to `level`.
void fm_flatten(Fm *fm, uint32_t level);

// Must be called with the new height of the file manager when the Ui is
// resized.
void fm_on_resize(Fm *fm, uint32_t height);
