#include "dir.h"

#include "defs.h"
#include "file.h"
#include "log.h"
#include "memory.h"
#include "path.h"
#include "sha256.h"
#include "sort.h"
#include "stcutil.h"
#include "util.h"

#include <stc/cstr.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static inline void load_dircount_cached(Dir *dir, File *file);
static inline void trim_dircount_cache(Dir *dir, uint32 num_dirs);

static inline void drop_files(Dir *dir);

// queue node to load flattened dirs
typedef struct flat_dir_node {
  const char *path; // path to load
  i32 level;        // depth from the root
  bool hidden;      // true if any cmponent of path began with .
} node;

#define i_type queue_dirs
#define i_key struct flat_dir_node
#include <stc/queue.h>

// define templated sorting functions

#define i_type files_natural, File *
#define i_cmp compare_natural
#include <stc/sort.h>

#define i_type files_name, File *
#define i_cmp compare_name
#include <stc/sort.h>

#define i_type files_size, File *
#define i_cmp compare_size
#include <stc/sort.h>

#define i_type files_atime, File *
#define i_cmp compare_atime
#include <stc/sort.h>

#define i_type files_ctime, File *
#define i_cmp compare_ctime
#include <stc/sort.h>

#define i_type files_mtime, File *
#define i_cmp compare_mtime
#include <stc/sort.h>

#define i_type files_key, File *
#define i_cmp compare_key
#include <stc/sort.h>

const char *fileinfo_str[] = {"size", "atime", "ctime", "mtime"};

// doesn't check bounds
static inline void vec_file_set(vec_file *vec, usize i, File *file) {
  vec->data[i] = file;
}

#define vec_file_qsort(vec, cmp)                                               \
  do {                                                                         \
    qsort(vec.data, vec.size, sizeof *vec.data, cmp);                          \
  } while (0)

static inline void reverse(File **a, usize len) {
  for (usize i = 0; i < len / 2; i++) {
    c_swap(a + i, a + len - i - 1);
  }
}

// does not attempt to keep the cursor position
static void apply_filters(Dir *d) {
  if (d->filter) {
    usize i = 0;
    c_foreach(it, vec_file, d->files_sorted) {
      if (filter_match(d->filter, *it.ref)) {
        vec_file_set(&d->files, i++, *it.ref);
      } else {
        (*it.ref)->score = 0;
      }
    }
    d->files.size = i;
    if (filter_cmp(d->filter)) {
      vec_file_qsort(d->files, filter_cmp(d->filter));
    }
  } else {
    memcpy(d->files.data, d->files_sorted.data,
           d->files_sorted.size * sizeof(File *));
    d->files.size = d->files_sorted.size;
  }
  d->ind = max(min(d->ind, vec_file_size(&d->files) - 1), 0);
}

void dir_apply_random_keys(Dir *dir, u64 salt) {
  u8 hash[32];
  SHA256_CTX ctx;
  if (salt)
    dir->settings.salt = salt;
  else
    salt = dir->settings.salt;

  c_foreach(it, Dir, dir) {
    File *file = *it.ref;
    zsview name = file_name(file);

    sha256_init(&ctx);
    sha256_update(&ctx, (u8 *)name.str, name.size);
    sha256_update(&ctx, (u8 *)&salt, sizeof salt);
    sha256_final(&ctx, hash);
    file->key = 0x7FFFFFFFFFFFFFFF & *(u64 *)hash; // null highest bit
  }
}

/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(Dir *d, bool force) {
  if (vec_file_is_empty(&d->files_all)) {
    d->sorted = true;
    return;
  }
  if (force || !d->sorted) {
    switch (d->settings.sorttype) {
    case SORT_NATURAL:
      files_natural_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_NAME:
      files_name_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_SIZE:
      files_size_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_ATIME:
      files_atime_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_CTIME:
      files_ctime_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_MTIME:
      files_mtime_sort(d->files_all.data, d->files_all.size);
      break;
    case SORT_LUA:
    case SORT_RAND:
      files_key_sort(d->files_all.data, d->files_all.size);
    default:
      break;
    }
    d->sorted = true;
  }
  usize num_dirs = 0;
  usize j = 0;
  if (d->settings.hidden) {
    if (d->settings.dirfirst) {
      /* first pass: directories */
      c_foreach(it, vec_file, d->files_all) {
        if (file_isdir(*it.ref)) {
          d->files_sorted.data[j++] = *it.ref;
        }
      }
      num_dirs = j;
      /* second pass: files */
      c_foreach(it, vec_file, d->files_all) {
        if (!file_isdir(*it.ref)) {
          d->files_sorted.data[j++] = *it.ref;
        }
      }
    } else {
      j = vec_file_size(&d->files_all);
      memcpy(d->files_sorted.data, d->files.data, j * sizeof(File *));
    }
  } else {
    if (d->settings.dirfirst) {
      c_foreach(it, vec_file, d->files_all) {
        if (!file_hidden(*it.ref) && file_isdir(*it.ref)) {
          d->files_sorted.data[j++] = *it.ref;
        }
      }
      num_dirs = j;
      c_foreach(it, vec_file, d->files_all) {
        if (!file_hidden(*it.ref) && !file_isdir(*it.ref)) {
          d->files_sorted.data[j++] = *it.ref;
        }
      }
    } else {
      c_foreach(it, vec_file, d->files_all) {
        if (!file_hidden(*it.ref)) {
          d->files_sorted.data[j++] = *it.ref;
        }
      }
    }
  }
  d->files_sorted.size = j;
  d->files.size = j;
  if (d->settings.reverse) {
    reverse(d->files_sorted.data, num_dirs);
    reverse(d->files_sorted.data + num_dirs, d->files_sorted.size - num_dirs);
  }

  apply_filters(d);
}

void dir_filter(Dir *dir, Filter *filter, u32 height, u32 scrolloff) {
  File *file = dir_current_file(dir);
  if (dir->filter) {
    filter_destroy(dir->filter);
    dir->filter = NULL;
  }
  dir->filter = filter;
  apply_filters(dir);
  dir_move_cursor_to_ptr(dir, file, height, scrolloff);
}

bool dir_check(const Dir *dir) {
  struct stat statbuf;
  if (unlikely(stat(dir_path_str(dir), &statbuf) == -1)) {
    log_perror("stat");
    return false;
  }
  return statbuf.st_mtime <= dir->load_time;
}

Dir *dir_create(zsview path) {
  Dir *dir = xcalloc(1, sizeof *dir);
  dir->path = cstr_from_zv(path);
  dir->name = basename_zv(cstr_zv(&dir->path));
  dir->load_time = time(NULL);
  return dir;
}

static inline void load_dircount_cached(Dir *dir, File *file) {
  map_str_int_iter it = map_str_int_find(&dir->dircounts, file_name_str(file));
  if (it.ref) {
    struct tuple_mtime_count tup = it.ref->second;
    if (tup.mtime == file->stat.st_mtim.tv_sec) {
      // use cached data
      file_set_dircount(file, tup.count);
    } else {
      // update the cache
      it.ref->second.mtime = file->stat.st_mtim.tv_sec;
      it.ref->second.count = file_load_dircount(file);
    }
  } else {
    // add new data to cache
    struct tuple_mtime_count tup = {
        .mtime = file->stat.st_mtim.tv_sec,
        .count = file_load_dircount(file),
    };
    map_str_int_emplace(&dir->dircounts, file_name_str(file), tup);
  }
}

// re-creates the hash map if it has over 50% stale entries
static inline void trim_dircount_cache(Dir *dir, uint32 num_dirs) {
  usize cache_size = map_str_int_size(&dir->dircounts);
  if (cache_size > 16 && cache_size > 2 * num_dirs) {
    map_str_int_clear(&dir->dircounts);
    c_foreach(it, vec_file, dir->files_all) {
      if (file_isdir(*it.ref)) {
        struct tuple_mtime_count tup = {
            .mtime = (*it.ref)->stat.st_mtim.tv_sec,
            .count = file_dircount(*it.ref),
        };
        map_str_int_emplace(&dir->dircounts, file_name_str(*it.ref), tup);
      }
    }
    // shrink, but leave some space for possible inserts, load factor is 0.8
    // (which would be exactly factor 1.25)
    map_str_int_reserve(&dir->dircounts, (long)(num_dirs * 1.3));
  }
}

Dir *dir_load(zsview path, map_str_int dircounts, bool load_fileinfo,
              atomic_bool *stop) {
  Dir *dir = dir_create(path);
  dir->has_fileinfo = load_fileinfo;
  dir->dircounts = dircounts;
  if (!load_fileinfo)
    stop = NULL;

  if (unlikely(stat(path.str, &dir->stat) == -1)) {
    log_perror("stat");
    dir->error = errno;
    return dir;
  }

  DIR *dirp = opendir(path.str);
  if (unlikely(dirp == NULL)) {
    log_perror("opendir");
    dir->error = errno;
    return dir;
  }
  i32 dir_fd = open(path.str, O_RDONLY);
  if (unlikely(dir_fd < 0)) {
    log_perror("open");
    dir->error = errno;
    closedir(dirp);
    return dir;
  }

  vec_file files = vec_file_init();
  usize num_dirs = 0;

  struct dirent *entry;
  while ((entry = readdir(dirp))) {
    if (path_is_dot_or_dotdot(entry->d_name))
      continue;

    File *file = file_create(path.str, entry->d_name, dir_fd, load_fileinfo);
    if (file != NULL) {
      if (load_fileinfo && file_isdir(file)) {
        num_dirs++;
        load_dircount_cached(dir, file);
      }
      vec_file_push(&files, file);
    }
    if (stop && atomic_load_explicit(stop, memory_order_relaxed)) {
      break;
    }
  }
  closedir(dirp);
  close(dir_fd);

  vec_file_shrink_to_fit(&files);
  dir->files_all = vec_file_clone(files);
  dir->files_sorted = vec_file_clone(files);
  dir->files = files;

  if (load_fileinfo) {
    trim_dircount_cache(dir, num_dirs);
  }

  dir->status = DIR_LOADING_FULLY;
  dir->loading = false;

  return dir;
}

Dir *dir_load_flat(zsview path, i32 level, map_str_int dircounts,
                   bool load_fileinfo, atomic_bool *stop) {
  Dir *dir = dir_create(path);
  dir->has_fileinfo = load_fileinfo;
  dir->dircounts = dircounts;

  if (unlikely(level < 0))
    level = 0;
  dir->flatten_level = level;

  if (unlikely(lstat(path.str, &dir->stat) == -1)) {
    log_perror("lstat");
    dir->error = errno;
    return dir;
  }

  vec_file files = vec_file_init();

  struct queue_dirs queue = queue_dirs_init();
  queue_dirs_push(&queue, (node){path.str, 0, false});

  usize num_dirs = 0;

  while (!queue_dirs_is_empty(&queue)) {
    if (atomic_load_explicit(stop, memory_order_relaxed))
      break;

    node head = *queue_dirs_front(&queue);
    queue_dirs_pop(&queue);

    DIR *dirp = opendir(head.path);
    if (unlikely(dirp == NULL))
      continue;

    i32 dir_fd = open(head.path, O_RDONLY);
    if (unlikely(dir_fd < 0)) {
      closedir(dirp);
      continue;
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
      if (path_is_dot_or_dotdot(entry->d_name))
        continue;

      File *file = file_create(head.path, entry->d_name, dir_fd, load_fileinfo);
      if (file != NULL) {
        file->hidden |= head.hidden;
        if (file_isdir(file)) {
          if (head.level + 1 <= level) {
            queue_dirs_push(&queue, (node){
                                        file_path_str(file),
                                        head.level + 1,
                                        file_hidden(file),
                                    });
          }
        }
        // name is a pointer into path, we can simply move it back
        i32 pos = 0;
        for (i32 i = 0; i < head.level; i++) {
          pos -= 2;
          while (file->name.str[pos - 1] != '/') {
            pos--;
          }
        }
        file->name.str += pos;
        file->name.size -= pos;

        if (load_fileinfo && file_isdir(file)) {
          num_dirs++;
          load_dircount_cached(dir, file);
        }

        vec_file_push(&files, file);
      }
    }
    closedir(dirp);
    close(dir_fd);
  }
  queue_dirs_drop(&queue);

  vec_file_shrink_to_fit(&files);
  dir->files_all = vec_file_clone(files);
  dir->files_sorted = vec_file_clone(files);
  dir->files = files;

  if (load_fileinfo) {
    trim_dircount_cache(dir, num_dirs);
  }

  return dir;
}

int dir_move_cursor(Dir *d, i32 ct, u32 height, u32 scrolloff) {
  u32 prev = d->ind;
  d->ind = max(min(d->ind + ct, dir_length(d) - 1), 0);
  if (ct < 0) {
    d->pos = min(max(scrolloff, d->pos + ct), d->ind);
  } else {
    d->pos = max(min(height - 1 - scrolloff, d->pos + ct),
                 height - dir_length(d) + d->ind);
  }
  return d->ind != prev;
}

static inline bool dir_cursor_move_to_sel(Dir *d, u32 height, u32 scrolloff) {
  if (cstr_is_empty(&d->sel) || vec_file_is_empty(&d->files)) {
    return true;
  }
  bool ret = false;

  i32 i = 0;
  c_foreach(it, vec_file, d->files) {
    if (cstr_equals_zv(&d->sel, file_name(*it.ref))) {
      dir_move_cursor(d, i - d->ind, height, scrolloff);
      ret = true;
      break;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));

  cstr_clear(&d->sel);
  return ret;
}

static inline bool dir_cursor_move_to_ino(Dir *d, dev_t dev, ino_t ino,
                                          u32 height, u32 scrolloff) {
  i32 i = 0;
  c_foreach(it, vec_file, d->files) {
    if ((*it.ref)->lstat.st_dev == dev && (*it.ref)->lstat.st_ino == ino) {
      dir_move_cursor(d, i - d->ind, height, scrolloff);
      return true;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));
  return false;
}

void dir_move_cursor_to_name(Dir *d, zsview name, u32 height, u32 scrolloff) {
  if (unlikely(zsview_is_empty(name)))
    return;

  if (unlikely(vec_file_is_empty(&d->files))) {
    cstr_assign_zv(&d->sel, name);
    return;
  }

  i32 i = 0;
  c_foreach(it, vec_file, d->files) {
    if (zsview_eq2(file_name(*it.ref), name)) {
      dir_set_cursor(d, i, height, scrolloff);
      return;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));
}

int dir_move_cursor_to_ptr(Dir *dir, const File *file, u32 height,
                           u32 scrolloff) {
  if (!file)
    return false;
  i32 i = 0;
  c_foreach(it, Dir, dir) {
    if (*it.ref == file)
      return dir_set_cursor(dir, i, height, scrolloff);
    i++;
  }
  dir->ind = min(dir->ind, dir_length(dir));
  return true;
}

static inline void apply_scroll(Dir *dir, u32 height, u32 scrolloff) {
  (void)scrolloff;
  i32 num_files = vec_file_size(&dir->files);
  i32 files_above_viewport = dir->ind - dir->pos;
  i32 num_files_below_cursor = num_files - dir->ind - 1;
  i32 space_below_last_file = height - dir->pos - num_files_below_cursor - 1;
  if (files_above_viewport > 0 && space_below_last_file > 0) {
    dir->pos += min(files_above_viewport, space_below_last_file);
  }
}

void dir_update_with(Dir *dir, Dir *update, u32 height, u32 scrolloff) {
  // will try to select the file the cursor is on, dev/inode take priority
  // in case of a rename. Otherwise, we use the name.
  // TODO: why do we store both ino and file name?
  struct {
    dev_t dev;
    ino_t ino;
  } sel = {0};

  if (cstr_is_empty(&dir->sel) && dir->ind < dir_length(dir)) {
    File *file = dir_current_file(dir);
    sel.dev = file->lstat.st_dev;
    sel.ino = file->lstat.st_ino;
  }

  drop_files(dir);

  dir->files_all = vec_file_move(&update->files_all);
  dir->files_sorted = vec_file_move(&update->files_sorted);
  dir->files = vec_file_move(&update->files);

  dir->dircounts = map_str_int_move(&update->dircounts);

  dir->load_time = update->load_time;
  dir->error = update->error;
  dir->flatten_level = update->flatten_level;
  dir->stat = update->stat;
  dir->status = DIR_LOADING_FULLY;
  dir->loading = false;

  dir_sort(dir, true);

  // TODO: if the cursor rest in the middle of the viewport, and files are
  // inserted above, the cursor is moved down, instead we could keep the cursor
  // position and scroll
  // I think in general we should try to keep dir->pos stable here, if possible
  if (!cstr_is_empty(&dir->sel)) {
    dir_cursor_move_to_sel(dir, height, scrolloff);
  } else {
    dir_cursor_move_to_ino(dir, sel.dev, sel.ino, height, scrolloff);
  }
  apply_scroll(dir, height, scrolloff);

  dir_destroy(update);
}

static inline void drop_files(Dir *dir) {
  c_foreach(it, vec_file, dir->files_all) {
    file_destroy(*it.ref);
  }
  vec_file_drop(&dir->files_all);
  vec_file_drop(&dir->files_sorted);
  vec_file_drop(&dir->files);
}

static inline void drop_fields(Dir *dir) {
  cstr_drop(&dir->path);
  drop_files(dir);
  filter_destroy(dir->filter);
  cstr_drop(&dir->sel);
  hmap_cstr_drop(&dir->tags.tags);
  map_str_int_drop(&dir->dircounts);
}

void dir_destroy(Dir *dir) {
  if (likely(dir)) {
    assert(dir->refcount == 0);
    drop_fields(dir);
    xfree(dir);
  }
}

Dir *dir_inc_ref(Dir *dir) {
  atomic_fetch_add(&dir->refcount, 1);
  return dir;
}

void dir_dec_ref(Dir *dir) {
  assert(dir->refcount > 0);
  if (unlikely(atomic_fetch_sub(&dir->refcount, 1) == 1)) {
    dir_destroy(dir);
  }
}

void dir_unload(Dir *dir) {
  cstr path = cstr_move(&dir->path);

  drop_fields(dir);

  memset(dir, 0, sizeof *dir);
  dir->path = path;
  dir->name = basename_zv(cstr_zv(&dir->path));
  dir->load_time = time(NULL);
}

i32 fileinfo_from_str(const char *str) {
  for (i32 i = 0; i < NUM_FILEINFO; i++) {
    if (streq(str, fileinfo_str[i])) {
      return i;
    }
  }
  return -1;
}

bool dir_scroll_up(Dir *dir, u32 height, u32 scrolloff) {
  if (dir->ind > 0 && dir->ind == dir->pos)
    return dir_move_cursor(dir, -1, height, scrolloff);

  if (dir->pos < height - scrolloff - 1) {
    dir->pos++;
  } else {
    dir->pos = height - scrolloff - 1;
    dir->ind--;
    if (dir->ind > dir_length(dir) - scrolloff - 1)
      dir->ind = dir_length(dir) - scrolloff - 1;
    return true;
  }
  return false;
}

bool dir_scroll_down(Dir *dir, u32 height, u32 scrolloff) {
  if (dir_length(dir) - dir->ind + dir->pos - 1 < height)
    return dir_move_cursor(dir, -1, height, scrolloff);

  if (dir->pos > scrolloff) {
    dir->pos--;
  } else {
    dir->pos = scrolloff;
    dir->ind++;
    if (dir->ind < dir->pos)
      dir->ind = dir->pos;
    return true;
  }
  return false;
}
