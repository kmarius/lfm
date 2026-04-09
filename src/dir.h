#pragma once

#include "defs.h"
#include "file.h"
#include "filter.h"
#include "path.h"
#include "sort.h"
#include "types/hmap_cstr.h"

#include <stc/cstr.h>
#include <stc/zsview.h>

#include <stdatomic.h>
#include <stdbool.h>

#define i_type vec_file, File *
#include <stc/vec.h>

// we might have to use the full st_mtime, not just the seconds part
struct tuple_mtime_count {
  time_t mtime;
  usize count;
};

#define i_type map_str_int
#define i_keypro cstr
#define i_val struct tuple_mtime_count
#include <stc/hmap.h>

typedef enum {
  INFO_SIZE = 0,
  INFO_ATIME,
  INFO_CTIME,
  INFO_MTIME,
  NUM_FILEINFO
} fileinfo;

i32 fileinfo_from_str(const char *str);

typedef enum {
  DIR_LOADING_DELAYED = 0,
  DIR_LOADING_INITIAL,
  DIR_LOADING_FULLY,
  DIR_DISOWNED, // removed from loader cache, should not be scheduled
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
  // refcounting dirs makes reloading and passing dirs to lua much easier.
  // Currently, we count refs for direcories in the loader/dir cache,
  // when a directory is requested to be reloaded and whenever a reference
  // is passed to lua
  atomic_uint refcount;
  cstr path;
  zsview name;

  struct stat stat;

  vec_file files;        // every visible file
  vec_file files_all;    // every file in the directory
  vec_file files_sorted; // every file, but sorted

  // maps name -> (mtime, dircount) for every directory in this directory
  // we hand it over to the thread that will reload the directory
  // and get it back in the update
  map_str_int dircounts;

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
  u32 cookie;

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

Dir *dir_inc_ref(Dir *dir);
void dir_dec_ref(Dir *dir);

// Bring the directory back into its "unloaded" state.
void dir_unload(Dir *dir);

// Replace files and metadata of `dir` with those of `update`. Frees `update`.
void dir_update_with(Dir *dir, Dir *update, u32 height, u32 scrolloff);

// Loads the directory at `path` from disk. Additionally count the files in
// each subdirectory if `load_fileinfo` is `true`. If `load_fileinfo` is
// `true` and a `stop` signal is passed, it is read with relaxed ordering
// after each file to possibly abort early.
Dir *dir_load(zsview path, map_str_int dircounts, bool load_fileinfo,
              atomic_bool *stop);

// Load a flat directorie showing files up `level`s deep.
Dir *dir_load_flat(zsview path, i32 level, map_str_int dircounts,
                   bool load_dircount, atomic_bool *stop);

static inline usize dir_length(const Dir *dir) {
  return vec_file_size(&dir->files);
}

static inline zsview dir_path(const Dir *dir) {
  return cstr_zv(&dir->path);
}

static inline const char *dir_path_str(const Dir *dir) {
  return cstr_str(&dir->path);
}

static inline zsview dir_name(const Dir *dir) {
  return dir->name;
}

// Is the directory in the process of being loaded?
static inline bool dir_loading(const Dir *dir) {
  return dir->status < DIR_LOADING_FULLY;
}

// Current file of `dir`. Can be `NULL` if it is empty or not yet loaded, or
// if files are filtered/hidden.
__lfm_nonnull()
static inline File *dir_current_file(const Dir *dir) {
  if (unlikely(dir->ind >= dir_length(dir)))
    return NULL;
  return *vec_file_at(&dir->files, dir->ind);
}

// Sort `dir` with respect to `dir->hidden`, `dir->dirfirst`, `dir->reverse`,
// `dir->sorttype`. If `force` is not set, the directory is only sorted if its
// `sorted` flag is false. Filters are always applied.
void dir_sort(Dir *dir, bool force);

// Applies the filter to `dir`. `NULL` clears the
// filter. Attempts to re-select the previously selected file.
void dir_filter(Dir *dir, Filter *filter, u32 height, u32 scrolloff);

// Check `dir` for changes on disk by comparing mtime. Returns `true` if there
// are no changes, `false` otherwise.
bool dir_check(const Dir *dir);

// Move the cursor in the current dir by `ct`, respecting the `scrolloff`
// setting by passing it and the current `height` of the viewport.
int dir_move_cursor(Dir *dir, i32 ct, u32 height, u32 scrolloff);

int dir_move_cursor_to_ptr(Dir *dir, const File *file, u32 height,
                           u32 scrolloff);

static inline bool dir_set_cursor(Dir *dir, u32 ind, u32 height,
                                  u32 scrolloff) {
  return dir_move_cursor(dir, ind - dir->ind, height, scrolloff);
}

// Move the cursor in the current dir to the file `name`, respecting the
// `scrolloff` setting by passing it and the current `height` of the viewport.
void dir_move_cursor_to_name(Dir *dir, zsview name, u32 height, u32 scrolloff);

// Scroll up the directory while keeping the cursor position if possible.
bool dir_scroll_up(Dir *dir, u32 height, u32 scrolloff);

// Scroll down the directory while keeping the cursor position if possible.
bool dir_scroll_down(Dir *dir, u32 height, u32 scrolloff);

// Returns true `d` is the root directory.
static inline bool dir_is_root(const Dir *dir) {
  return path_is_root(dir_path(dir));
}

// define iterators so we can use c_foreach with Dir
#define Dir_iter vec_file_iter
#define Dir_next vec_file_next
#define Dir_begin(dir) vec_file_begin(dir->files)
