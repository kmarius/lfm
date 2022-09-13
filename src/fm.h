#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cvector.h"
#include "dir.h"
#include "file.h"
#include "hashtab.h"

enum paste_mode_e {
  PASTE_MODE_MOVE,
  PASTE_MODE_COPY,
};

typedef struct fm_s {

  uint32_t height; // height of the ui

  struct {
    // Visible directories excluding preview
    cvector_vector_type(Dir *) visible;
    uint32_t length;

    // preview directory, NULL if there is none, e.g. if the cursor is resting on a file
    Dir *preview;
  } dirs;

  struct {
    // Current and previous selection (needed for visual mode)
    /* TODO: consider checking at some point if selected files even still exist (on 2022-08-03) */
    LinkedHashtab *paths;
    Hashtab *previous;
  } selection;

  struct {
    // Copy/move buffer, vector of paths
    LinkedHashtab *buffer;
    enum paste_mode_e mode;
  } paste;

  struct {
    bool active;
    uint32_t anchor;
  } visual;

  char *automark;
} Fm;

// Moves to the correct starting directory, loads initial dirs and sets up
// previews and inotify watchers.
void fm_init(Fm *fm);

// Unloads directories and frees all resources.
void fm_deinit(Fm *fm);

// Updates the number of loaded directories, called after the number of columns
// changes.
void fm_recol(Fm *fm);

// Current directory. Never `NULL`.
#define fm_current_dir(fm) (fm)->dirs.visible[0]

// Current file of the current directory. Can be `NULL`.
#define fm_current_file(fm) dir_current_file(fm_current_dir(fm))

#define fm_preview_dir(fm) (fm)->preview

// Move the cursor relative to the current position.
bool fm_cursor_move(Fm *fm, int32_t ct);

static inline void fm_cursor_move_to_ind(Fm *fm, uint32_t ind)
{
  fm_cursor_move(fm, ind - fm_current_dir(fm)->ind);
}

// Move cursor `ct` up in the current directory.
static inline bool fm_up(Fm *fm, int32_t ct)
{
  return fm_cursor_move(fm, -ct);
}

// Move cursor `ct` down in the current directory.
static inline bool fm_down(Fm *fm, int32_t ct)
{
  return fm_cursor_move(fm, ct);
}

// Move cursor to the top of the current directory.
static inline bool fm_top(Fm *fm)
{
  return fm_up(fm, fm_current_dir(fm)->ind);
}

// Move cursor to the bottom of the current directory.
static inline bool fm_bot(Fm *fm)
{
  return fm_down(fm, fm_current_dir(fm)->length - fm_current_dir(fm)->ind);
}

// Scroll up the directory while keeping the cursor position if possible.
bool fm_scroll_up(Fm *fm);

// Scroll down the directory while keeping the cursor position if possible.
bool fm_scroll_down(Fm *fm);

// Changes directory to the directory given by `path`. If `save` then the current
// directory will be saved as the special "'" automark. Returns `true´ if the directory has been changed.
bool fm_chdir(Fm *fm, const char *path, bool save);

// Open the currently selected file: if it is a directory, chdir into it.
// Otherwise return the file so that the caller can open it.
File *fm_open(Fm *fm);

// Chdir to the parent of the current directory.
bool fm_updir(Fm *fm);

// Move the cursor the file with `name` if it exists. Otherwise leaves the
// cursor at the closest valid position (i.e. after the number of files
// decreases)
void fm_move_cursor_to(Fm *fm, const char *name);

// Lfmly the filter string given by `filter` to the current directory.
void fm_filter(Fm *fm, const char *filter);

// Return the filter string of the currently selected directory.
static inline const char *fm_filter_get(const Fm *fm)
{
  return filter_string(fm_current_dir(fm)->filter);
}

//  Show hidden files.
void fm_hidden_set(Fm *fm, bool hidden);

// Checks all visible directories for changes on disk and schedules reloads if
// necessary.
void fm_check_dirs(const Fm *fm);

// jump to previous directory
static inline bool fm_jump_automark(Fm *fm)
{
  if (fm->automark) {
    return fm_chdir(fm, fm->automark, true);
  }
  return false;
}

// Toggles the selection of the currently selected file.
void fm_selection_toggle_current(Fm *fm);

// Add `path` to the current selection if not already contained.
static inline void fm_selection_add(Fm *fm, const char *path)
{
  char *val = strdup(path);
  lht_set(fm->selection.paths, val, val);
}

// Clear the selection completely.
static inline void fm_selection_clear(Fm *fm)
{
  lht_clear(fm->selection.paths);
}

// Reverse the file selection.
void fm_selection_reverse(Fm *fm);

// Begin visual selection mode.
void fm_selection_visual_start(Fm *fm);

// End visual selection mode.
void fm_selection_visual_stop(Fm *fm);

// Toggle visual selection mode.
void fm_selection_visual_toggle(Fm *fm);

// Write the current celection to the file given as `path`.
// Directories are created as needed.
void fm_selection_write(const Fm *fm, const char *path);

// Set the current selection into the load buffer with mode `mode`.
void fm_paste_mode_set(Fm *fm, enum paste_mode_e mode);

// Clear copy/move buffer.
static inline void fm_paste_buffer_clear(Fm *fm)
{
  lht_clear(fm->paste.buffer);
}

// Get the list of files in copy/move buffer. Returns a cvector of char*.
static inline const LinkedHashtab *fm_paste_buffer_get(const Fm *fm)
{
  return fm->paste.buffer;
}

static inline void fm_paste_buffer_add(Fm *fm, const char* file)
{
  char *val = strdup(file);
  lht_set(fm->paste.buffer, val, val);
}

// Get the mode current load, one of `MODE_COPY`, `MODE_MOVE`.
static inline enum paste_mode_e fm_paste_mode_get(const Fm *fm)
{
  return fm->paste.mode;
}

// Drop directory cache and reload visible directories from disk.
void fm_drop_cache(Fm *fm);

// Reload visible directories.
void fm_reload(Fm *fm);

// Update preview (e.g. after moving the cursor).
void fm_update_preview(Fm *fm);

// Flatten the current directory up to some level.
void fm_flatten(Fm *fm, uint32_t level);

void fm_resize(Fm *fm, uint32_t height);
