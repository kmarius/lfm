#pragma once

#include "file.h"
#include "filter.h"
#include "hmap_cstr.h"
#include "macros.h"
#include "path.h"
#include "sort.h"

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <stdbool.h>
#include <stdint.h>

#define i_type vec_file, File *
#include "stc/vec.h"

typedef enum {
  INFO_SIZE = 0,
  INFO_ATIME,
  INFO_CTIME,
  INFO_MTIME,
  NUM_FILEINFO
} fileinfo;

typedef enum {
  DIR_LOADING_DELAYED = 0,
  DIR_LOADING_INITIAL,
  DIR_LOADING_FULLY,
} dir_loading_status;

extern const char *fileinfo_str[NUM_FILEINFO];

struct dir_settings {
  bool hidden;
  bool dirfirst;
  bool reverse;
  sorttype sorttype;
  fileinfo fileinfo;
};

typedef struct Dir {
  cstr path;
  zsview name;

  struct stat stat;

  vec_file files;        // every visible file
  vec_file files_all;    // every file in the directory
  vec_file files_sorted; // every file, but sorted

  bool visible;
  dir_loading_status status;

  int32_t error; // shows errno if an error occured during loading, 0 otherwise

  time_t load_time;             // used to check for changes
  uint64_t last_loading_action; // Time (in milliseconds) at which the last
                                // action started after which a "loading"
                                // indicator should be shown for this directory.
                                // 0 if there is no loading/checking.

  uint64_t next_scheduled_load; // time of the next (or latest) scheduled reload
  uint64_t next_requested_load; // will be set if a reload is requested when
                                // one is already scheduled, otherwise 0
  bool loading;                 // is a reload in the process
  bool scheduled;               // is a reload scheduled

  uint32_t ind; // cursor position in files[]
  uint32_t pos; // cursor position in the ui, offset from the top row
  cstr sel;

  Filter *filter;

  uint32_t flatten_level;
  bool has_fileinfo;
  bool sorted;
  struct dir_settings settings;

  // maps name -> string; displays up to cols chars before the file, if enabled
  struct tags {
    hmap_cstr tags;
    int cols;
  } tags;
} Dir;

// Creates a directory, no files are loaded.
Dir *dir_create(zsview path);

// Loads the directory at `path` from disk. Additionally count the files in each
// subdirectory if `load_filecount` is `true`.
Dir *dir_load(zsview path, bool load_dircount);

// Free all resources belonging to `dir`.
void dir_destroy(Dir *dir);

static inline size_t dir_length(const Dir *dir) {
  return vec_file_size(&dir->files);
}

static inline const cstr *dir_path(const Dir *dir) {
  return &dir->path;
}

static inline const char *dir_path_str(const Dir *dir) {
  return cstr_str(&dir->path);
}

static inline const zsview *dir_name(const Dir *dir) {
  return &dir->name;
}

// Is the directory in the process of being loaded?
static inline bool dir_loading(const Dir *dir) {
  return dir->status < DIR_LOADING_FULLY;
}

// Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
// if files are filtered/hidden.
__lfm_nonnull()
static inline File *dir_current_file(const Dir *dir) {
  if (unlikely(dir->ind >= dir_length(dir))) {
    return NULL;
  }
  return *vec_file_at(&dir->files, dir->ind);
}

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
void dir_cursor_move_to(Dir *dir, zsview name, uint32_t height,
                        uint32_t scrolloff);

// Replace files and metadata of `dir` with those of `update`. Frees `update`.
void dir_update_with(Dir *dir, Dir *update, uint32_t height,
                     uint32_t scrolloff);

// Returns true `d` is the root directory.
static inline bool dir_isroot(const Dir *dir) {
  return path_is_root(dir_path(dir));
}

// Load a flat directorie showing files up `level`s deep.
Dir *dir_load_flat(zsview path, int level, bool load_dircount);

// define iterators so we can use c_foreach with Dir
#define Dir_iter vec_file_iter
#define Dir_next vec_file_next
#define Dir_begin(dir) vec_file_begin(dir->files)

int fileinfo_from_str(const char *str);
