#include "dir.h"

#include "file.h"
#include "log.h"
#include "memory.h"
#include "path.h"
#include "sort.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <curses.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <libgen.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// queue to load flattened dirs
typedef struct flat_dir_node {
  const char *path; // path to load
  int level;        // depth from the root
  bool hidden;      // true if any cmponent of path began with .
} node;

#define i_type queue_dirs
#define i_key struct flat_dir_node
#include "stc/queue.h"

// define templated sorting functions

#define i_type files_natural, File *
#define i_cmp compare_natural
#include "stc/sort.h"

#define i_type files_name, File *
#define i_cmp compare_name
#include "stc/sort.h"

#define i_type files_size, File *
#define i_cmp compare_size
#include "stc/sort.h"

#define i_type files_atime, File *
#define i_cmp compare_atime
#include "stc/sort.h"

#define i_type files_ctime, File *
#define i_cmp compare_ctime
#include "stc/sort.h"

#define i_type files_mtime, File *
#define i_cmp compare_mtime
#include "stc/sort.h"

const char *fileinfo_str[] = {"size", "atime", "ctime", "mtime"};

// doesn't check bounds
static inline void vec_file_set(vec_file *vec, size_t i, File *file) {
  vec->data[i] = file;
}

#define vec_file_qsort(vec, cmp)                                               \
  do {                                                                         \
    qsort(vec.data, vec.size, sizeof *vec.data, cmp);                          \
  } while (0)

static inline void reverse(File **a, size_t len) {
  for (size_t i = 0; i < len / 2; i++) {
    c_swap(a + i, a + len - i - 1);
  }
}

// does not attempt to keep the cursor position
static void apply_filters(Dir *d) {
  if (d->filter) {
    size_t i = 0;
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

/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(Dir *d) {
  if (vec_file_is_empty(&d->files_all)) {
    d->sorted = true;
    return;
  }
  if (!d->sorted) {
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
    case SORT_RAND:
      shuffle(d->files_all.data, d->files_all.size, sizeof(File *));
    default:
      break;
    }
    d->sorted = true;
  }
  size_t num_dirs = 0;
  size_t j = 0;
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

void dir_filter(Dir *d, Filter *filter) {
  if (d->filter) {
    filter_destroy(d->filter);
    d->filter = NULL;
  }
  d->filter = filter;
  apply_filters(d);
}

bool dir_check(const Dir *dir) {
  struct stat statbuf;
  if (stat(dir_path_str(dir), &statbuf) == -1) {
    log_error("stat: %s", strerror(errno));
    return false;
  }
  return statbuf.st_mtime <= dir->load_time;
}

Dir *dir_create(zsview path) {
  Dir *dir = xcalloc(1, sizeof *dir);

  if (path.str[0] != '/') {
    char buf[PATH_MAX + 1];
    if (realpath(path.str, buf) == NULL) {
      snprintf(buf, sizeof buf - 1, "error: %s\n", strerror(errno));
    } else {
      dir->path = cstr_from(buf);
    }
  } else {
    dir->path = cstr_from_zv(path);
  }

  dir->load_time = time(NULL);
  int pos = basename(cstr_data(&dir->path)) - cstr_str(&dir->path);
  dir->name = zsview_from_pos(cstr_zv(&dir->path), pos);

  return dir;
}

Dir *dir_load(zsview path, bool load_fileinfo) {
  Dir *dir = dir_create(path);
  dir->has_fileinfo = load_fileinfo;

  if (stat(path.str, &dir->stat) == -1) {
    // TODO: figure out if/how we should handle errors here, we currently
    // only use the inode to check if we should reload
    //
    // also: do we need lstat?
    log_debug("lstat: %s", strerror(errno));
  }

  DIR *dirp = opendir(path.str);
  if (!dirp) {
    log_error("opendir: %s", strerror(errno));
    dir->error = errno;
    return dir;
  }

  vec_file files = vec_file_init();

  struct dirent *entry;
  while ((entry = readdir(dirp))) {
    if (path_is_dot_or_dotdot(entry->d_name)) {
      continue;
    }

    File *file = file_create(path.str, entry->d_name, load_fileinfo);
    if (file != NULL) {
      vec_file_push(&files, file);
    }
  }
  closedir(dirp);

  vec_file_shrink_to_fit(&files);
  dir->files_all = vec_file_clone(files);
  dir->files_sorted = vec_file_clone(files);
  dir->files = files;

  dir->status = DIR_LOADING_FULLY;
  dir->loading = false;

  return dir;
}

Dir *dir_load_flat(zsview path, int level, bool load_fileinfo) {
  Dir *dir = dir_create(path);
  dir->has_fileinfo = load_fileinfo;
  if (level < 0)
    level = 0;
  dir->flatten_level = level;

  if (lstat(path.str, &dir->stat) == -1) {
    // TODO: currently not saving an error if we can't read the root
    log_debug("lstat: %s", strerror(errno));
  }

  vec_file files = vec_file_init();

  struct queue_dirs queue = queue_dirs_init();
  queue_dirs_push(&queue, (node){path.str, 0, false});

  while (!queue_dirs_is_empty(&queue)) {
    node head = *queue_dirs_front(&queue);
    queue_dirs_pop(&queue);

    DIR *dirp = opendir(head.path);
    if (!dirp) {
      continue;
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
      if (path_is_dot_or_dotdot(entry->d_name)) {
        continue;
      }

      File *file = file_create(head.path, entry->d_name, load_fileinfo);
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
        int pos = 0;
        for (int i = 0; i < head.level; i++) {
          pos -= 2;
          while (file->name.str[pos - 1] != '/') {
            pos--;
          }
        }
        file->name.str += pos;
        file->name.size -= pos;

        vec_file_push(&files, file);
      }
    }
    closedir(dirp);
  }
  queue_dirs_drop(&queue);

  vec_file_shrink_to_fit(&files);
  dir->files_all = vec_file_clone(files);
  dir->files_sorted = vec_file_clone(files);
  dir->files = files;

  return dir;
}

void dir_cursor_move(Dir *d, int32_t ct, uint32_t height, uint32_t scrolloff) {
  d->ind = max(min(d->ind + ct, dir_length(d) - 1), 0);
  if (ct < 0) {
    d->pos = min(max(scrolloff, d->pos + ct), d->ind);
  } else {
    d->pos = max(min(height - 1 - scrolloff, d->pos + ct),
                 height - dir_length(d) + d->ind);
  }
  d->dirty = true;
}

static inline void dir_cursor_move_to_sel(Dir *d, uint32_t height,
                                          uint32_t scrolloff) {
  if (cstr_is_empty(&d->sel) || vec_file_is_empty(&d->files)) {
    return;
  }

  int i = 0;
  c_foreach(it, vec_file, d->files) {
    if (cstr_equals_zv(&d->sel, file_name(*it.ref))) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      break;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));

  cstr_clear(&d->sel);
}

static inline void dir_cursor_move_to_ino(Dir *d, dev_t dev, ino_t ino,
                                          uint32_t height, uint32_t scrolloff) {
  int i = 0;
  c_foreach(it, vec_file, d->files) {
    if ((*it.ref)->lstat.st_dev == dev && (*it.ref)->lstat.st_ino == ino) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      return;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));
}

void dir_cursor_move_to(Dir *d, zsview name, uint32_t height,
                        uint32_t scrolloff) {
  if (zsview_is_empty(name)) {
    return;
  }

  if (vec_file_is_empty(&d->files)) {
    cstr_assign_zv(&d->sel, name);
    return;
  }

  int i = 0;
  c_foreach(it, vec_file, d->files) {
    if (zsview_eq(file_name(*it.ref), &name)) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      return;
    }
    i++;
  }
  d->ind = min(d->ind, dir_length(d));
}

static inline void drop_files(Dir *dir) {
  c_foreach(it, vec_file, dir->files_all) {
    file_destroy(*it.ref);
  }
  vec_file_drop(&dir->files_all);
  vec_file_drop(&dir->files_sorted);
  vec_file_drop(&dir->files);
}

void dir_update_with(Dir *dir, Dir *update, uint32_t height,
                     uint32_t scrolloff) {
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
    const zsview *name = file_name(file);
    cstr_assign_zv(&dir->sel, *name);
  }

  drop_files(dir);

  dir->files_all = vec_file_move(&update->files_all);
  dir->files_sorted = vec_file_move(&update->files_sorted);
  dir->files = vec_file_move(&update->files);

  dir->load_time = update->load_time;
  dir->error = update->error;
  dir->flatten_level = update->flatten_level;
  dir->stat = update->stat;
  dir->status = DIR_LOADING_FULLY;
  dir->loading = false;

  dir->sorted = false;
  dir_sort(dir);

  if (sel.ino != 0) {
    dir_cursor_move_to_ino(dir, sel.dev, sel.ino, height, scrolloff);
  } else if (!cstr_is_empty(&dir->sel)) {
    // same file with different ino?
    dir_cursor_move_to_sel(dir, height, scrolloff);
  }
  cstr_clear(&dir->sel);

  dir_destroy(update);
}

void dir_destroy(Dir *dir) {
  if (dir) {
    drop_files(dir);
    filter_destroy(dir->filter);
    cstr_drop(&dir->sel);
    cstr_drop(&dir->path);
    hmap_cstr_drop(&dir->tags.tags);
    xfree(dir);
  }
}

int fileinfo_from_str(const char *str) {
  for (int i = 0; i < NUM_FILEINFO; i++) {
    if (streq(str, fileinfo_str[i])) {
      return i;
    }
  }
  return -1;
}
