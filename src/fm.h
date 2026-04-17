#pragma once

#include "dir.h"
#include "file.h"
#include "pathlist.h"
#include "selection.h"

#include <stc/types.h>

#include <stdbool.h>

#define i_type vec_dir, Dir *
#include <stc/vec.h>

struct lfm_opts;

typedef struct Fm {

  // Height of the fm.
  u32 height;

  // Not guaranteed to coincide with PWD or this processe's working directory,
  // which is only set once we know the destination is reachable.
  cstr pwd;

  struct {
    // Visible directories excluding preview. Index 0 is the current directory,
    // all following elements are the parents
    vec_dir visible;
    i32 max_visible;

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
    u32 anchor;
  } visual;

  // Previous directory, not changed on updir/open.
  cstr automark;
} Fm;

// Moves to the correct starting directory, loads initial dirs and sets up
// previews and inotify watchers.
void fm_init(Fm *fm, struct lfm_opts *opts);

// Unloads directories and frees all resources.
void fm_deinit(Fm *fm);

// Must be called with the new height of the file manager when the Ui is
// resized.
void fm_on_resize(Fm *fm, u32 height);

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

// Sort visible directories
void fm_sort(Fm *fm);

// Reload visible directories.
void fm_reload(Fm *fm);

// Checks all visible directories for changes on disk and schedules reloads if
// necessary.
void fm_check_dirs(const Fm *fm);

// jump to previous directory
static inline bool fm_jump_automark(Fm *fm) {
  if (cstr_is_empty(&fm->automark))
    return false;
  return fm_async_chdir(fm, cstr_zv(&fm->automark), true, true);
}

// Update directory perview after cursor moved
void fm_update_preview(Fm *fm);

// Drop directory cache and reload visible directories from disk.
void fm_drop_cache(Fm *fm);
