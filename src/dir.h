#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cvector.h"
#include "file.h"
#include "filter.h"

enum sorttype_e { SORT_NATURAL, SORT_NAME, SORT_SIZE, SORT_CTIME, SORT_RAND, };

typedef struct dir_s {
  char *path;
  char *name; // substring of path

  File **files_all;    // every file in the directory
  File **files_sorted; // every file, but sorted
  File **files;      // every visible file
  uint32_t length_all;
  uint32_t length_sorted;
  uint32_t length;

  bool visible;

  time_t load_time; // used to check for changes
  uint32_t updates; // number of applied updates
  int32_t error; // shows errno if an error occured during loading, 0 otherwise
  uint64_t next;

  uint32_t ind; // cursor position in files[]
  uint32_t pos; // cursor position in the ui, offset from the top row
  char *sel;

  Filter *filter;

  bool sorted;
  bool hidden;
  bool dirfirst;
  bool reverse;
  bool dircounts;
  enum sorttype_e sorttype;
  uint32_t flatten_level;
} Dir;

// Creates a directory, no files are loaded.
Dir *dir_create(const char *path);

// Loads the directory at `path` from disk. Additionally count the files in each
// subdirectory if `load_filecount` is `true`.
Dir *dir_load(const char *path, bool load_dircount);

// Free all resources belonging to `dir`.
void dir_destroy(Dir *dir);

// Is the directory in the process of being loaded?
static inline bool dir_loading(const Dir *dir)
{
  return dir->updates == 0;
}

// Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
// if files are filtered/hidden.
File *dir_current_file(const Dir *dir);

// Sort `dir` with respect to `dir->hidden`, `dir->dirfirst`, `dir->reverse`,
// `dir->sorttype`.
void dir_sort(Dir *dir);

// Returns the path of the parent of `dir` and `NULL` for the root directory.
const char *dir_parent_path(const Dir *dir);

// Lfmlies the filter string `filter` to `dir`. `NULL` or `""` clears the filter.
void dir_filter(Dir *dir, const char *filter);

// Check `dir` for changes on disk by comparing mtime. Returns `true` if there
// are no changes, `false` otherwise.
bool dir_check(const Dir *dir);

// Move the cursor in the current dir by `ct`, respecting the `scrolloff`
// setting by passing it and the current `height` of the viewport.
void dir_cursor_move(Dir *dir, int32_t ct, uint32_t height, uint32_t scrolloff);

// Move the cursor in the current dir to the file `name`, respecting the
// `scrolloff` setting by passing it and the current `height` of the viewport.
void dir_cursor_move_to(Dir *dir, const char *name, uint32_t height, uint32_t scrolloff);

// Replace files and metadata of `dir` with those of `update`. Frees `update`.
void dir_update_with(Dir *dir, Dir *update, uint32_t height, uint32_t scrolloff);

// Returns true `d` is the root directory.
inline bool dir_isroot(const Dir *dir)
{
  return (dir->path[0] == '/' && dir->path[1] == 0);
}

// Load a flat directorie showing files up `level`s deep.
Dir *dir_load_flat(const char *path, uint32_t level, bool load_dircount);
