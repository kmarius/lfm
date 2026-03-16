#pragma once

#include "defs.h"
#include "file.h"
#include "filter.h"
#include "path.h"
#include "sort.h"
#include "types/hmap_cstr.h"

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define i_type vec_file, File *
#include "stc/vec.h"

// we might have to use the full st_mtime, not just the seconds part
struct tuple_mtime_count {
  time_t mtime;
  usize count;
};

#define i_type hmap_dircount
#define i_keypro cstr
#define i_val struct tuple_mtime_count
#define c_pro_key
#include "stc/hmap.h"

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

  // maps name -> (mtime, dircount) for every directory in this directory
  // we hand it over to the thread that will reload the directory
  // and get it back in the update
  hmap_dircount dircounts;

  bool visible;
  dir_loading_status status;

  i32 error; // shows errno if an error occured during loading, 0 otherwise

  time_t load_time;        // used to check for changes
  u64 last_loading_action; // Time (in milliseconds) at which the last
                           // action started after which a "loading"
                           // indicator should be shown for this directory.
                           // 0 if there is no loading/checking.

  u64 next_scheduled_load; // time of the next (or latest) scheduled reload
  u64 next_requested_load; // will be set if a reload is requested when
                           // one is already scheduled, otherwise 0
  bool loading;            // is a reload in the process
  bool scheduled;          // is a reload scheduled

  u32 ind; // cursor position in files[]
  u32 pos; // cursor position in the ui, offset from the top row
  cstr sel;

  Filter *filter;

  u32 flatten_level;
  bool has_fileinfo;
  bool sorted;
  struct dir_settings settings;

  // maps name -> string; displays up to cols chars before the file, if enabled
  struct tags {
    hmap_cstr tags;
    i32 cols;
  } tags;
} Dir;

// Creates a directory, no files are loaded. Takes an absolute path.
Dir *dir_create(zsview path);

// Free all resources belonging to `dir`.
void dir_destroy(Dir *dir);

// Loads the directory at `path` from disk. Additionally count the files in each
// subdirectory if `load_fileinfo` is `true`.
// If `load_fileinfo` is `true` and a `stop` signal is passed,
// it is read with relaxed ordering after each file to possibly abort early.
Dir *dir_load(zsview path, hmap_dircount dircounts, bool load_fileinfo,
              atomic_bool *stop);

// Load a flat directorie showing files up `level`s deep.
Dir *dir_load_flat(zsview path, i32 level, hmap_dircount dircounts,
                   bool load_dircount, atomic_bool *stop);

static inline usize dir_length(const Dir *dir) {
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
void dir_cursor_move(Dir *dir, i32 ct, u32 height, u32 scrolloff);

// Move the cursor in the current dir to the file `name`, respecting the
// `scrolloff` setting by passing it and the current `height` of the viewport.
void dir_cursor_move_to(Dir *dir, zsview name, u32 height, u32 scrolloff);

// Replace files and metadata of `dir` with those of `update`. Frees `update`.
void dir_update_with(Dir *dir, Dir *update, u32 height, u32 scrolloff);

// Returns true `d` is the root directory.
static inline bool dir_isroot(const Dir *dir) {
  return path_is_root(dir_path(dir));
}

// define iterators so we can use c_foreach with Dir
#define Dir_iter vec_file_iter
#define Dir_next vec_file_next
#define Dir_begin(dir) vec_file_begin(dir->files)

i32 fileinfo_from_str(const char *str);
