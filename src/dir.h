#pragma once

#include "file.h"
#include "filter.h"
#include "sort.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  INFO_SIZE = 0,
  INFO_ATIME,
  INFO_CTIME,
  INFO_MTIME,
  NUM_FILEINFO
} fileinfo;

extern const char *fileinfo_str[NUM_FILEINFO];

struct dir_settings {
  bool hidden;
  bool dirfirst;
  bool reverse;
  sorttype sorttype;
  fileinfo fileinfo;
};

typedef struct Dir {
  char *path;
  char *name; // substring of path

  struct stat stat;

  File **files_all;    // every file in the directory
  File **files_sorted; // every file, but sorted
  File **files;        // every visible file
  uint32_t length_all;
  uint32_t length_sorted;
  uint32_t length;

  bool visible;

  int32_t error; // shows errno if an error occured during loading, 0 otherwise

  time_t load_time;             // used to check for changes
  uint32_t updates;             // number of applied updates
  uint64_t last_loading_action; // Time (in milliseconds) at which the last
                                // action started after which a "loading"
                                // indicator should be shown for this directory.
                                // 0 if there is no loading/checking.

  uint64_t next_scheduled_load; // time of the next (or latest) scheduled reload
  uint64_t next_requested_load; // will be set if a reload is requested when
                                // one is already scheduled, otherwise 0
  bool loading;                 // is a reload in the process
  bool scheduled;               // is a reload scheduled
  bool dirty;

  uint32_t ind; // cursor position in files[]
  uint32_t pos; // cursor position in the ui, offset from the top row
  char *sel;

  Filter *filter;

  uint32_t flatten_level;
  bool has_fileinfo;
  bool sorted;
  struct dir_settings settings;
} Dir;

// Creates a directory, no files are loaded.
Dir *dir_create(const char *path);

// Loads the directory at `path` from disk. Additionally count the files in each
// subdirectory if `load_filecount` is `true`.
Dir *dir_load(const char *path, bool load_dircount);

// Free all resources belonging to `dir`.
void dir_destroy(Dir *dir);

// Is the directory in the process of being loaded?
static inline bool dir_loading(const Dir *dir) {
  return dir->loading;
}

// Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
// if files are filtered/hidden.
File *dir_current_file(const Dir *dir);

// Sort `dir` with respect to `dir->hidden`, `dir->dirfirst`, `dir->reverse`,
// `dir->sorttype`.
void dir_sort(Dir *dir);

// Lfmlies the filter string `filter` to `dir`. `NULL` or `""` clears the
// filter.
void dir_filter(Dir *dir, Filter *filter);

// Check `dir` for changes on disk by comparing mtime. Returns `true` if there
// are no changes, `false` otherwise.
bool dir_check(const Dir *dir);

// Move the cursor in the current dir by `ct`, respecting the `scrolloff`
// setting by passing it and the current `height` of the viewport.
void dir_cursor_move(Dir *dir, int32_t ct, uint32_t height, uint32_t scrolloff);

// Move the cursor in the current dir to the file `name`, respecting the
// `scrolloff` setting by passing it and the current `height` of the viewport.
void dir_cursor_move_to(Dir *dir, const char *name, uint32_t height,
                        uint32_t scrolloff);

// Replace files and metadata of `dir` with those of `update`. Frees `update`.
void dir_update_with(Dir *dir, Dir *update, uint32_t height,
                     uint32_t scrolloff);

// Returns true `d` is the root directory.
static inline bool dir_isroot(const Dir *dir) {
  return (dir->path[0] == '/' && dir->path[1] == 0);
}

// Load a flat directorie showing files up `level`s deep.
Dir *dir_load_flat(const char *path, uint32_t level, bool load_dircount);
