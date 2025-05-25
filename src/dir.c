#include "dir.h"

#include "file.h"
#include "log.h"
#include "memory.h"
#include "path.h"
#include "sort.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <libgen.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define i_type vec_file, File *
#include "stc/vec.h"

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

File *dir_current_file(const Dir *d) {
  if (!d || d->ind >= d->length) {
    return NULL;
  }

  return d->files[d->ind];
}

static void apply_filters(Dir *d) {
  if (d->filter) {
    uint32_t j = 0;
    uint32_t up = 0;
    for (uint32_t i = 0; i < d->length_sorted; i++) {
      if (filter_match(d->filter, d->files_sorted[i])) {
        d->files[j++] = d->files_sorted[i];
      } else {
        if (i <= d->ind) {
          up++;
        }
        d->files_sorted[i]->score = 0;
      }
    }
    d->length = j;
    if (filter_cmp(d->filter)) {
      qsort(d->files, d->length, sizeof *d->files, filter_cmp(d->filter));
    }
    if (up > d->ind) {
      d->ind = 0;
    } else {
      d->ind -= up;
    }
  } else {
    /* TODO: try to select previously selected file
     * note that on the first call dir->files is not yet valid */
    memcpy(d->files, d->files_sorted, d->length_sorted * sizeof *d->files);
    d->length = d->length_sorted;
    d->ind = max(min(d->ind, d->length - 1), 0);
  }
}

static inline void swap(File **a, File **b) {
  File *Dir = *a;
  *a = *b;
  *b = Dir;
}

/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(Dir *d) {
  if (!d->files_all) {
    return;
  }
  if (!d->sorted) {
    switch (d->settings.sorttype) {
    case SORT_NATURAL:
      files_natural_sort(d->files_all, d->length_all);
      break;
    case SORT_NAME:
      files_name_sort(d->files_all, d->length_all);
      break;
    case SORT_SIZE:
      files_size_sort(d->files_all, d->length_all);
      break;
    case SORT_ATIME:
      files_atime_sort(d->files_all, d->length_all);
      break;
    case SORT_CTIME:
      files_ctime_sort(d->files_all, d->length_all);
      break;
    case SORT_MTIME:
      files_mtime_sort(d->files_all, d->length_all);
      break;
    case SORT_RAND:
      shuffle(d->files_all, d->length_all, sizeof *d->files_all);
    default:
      break;
    }
    d->sorted = 1;
  }
  uint32_t ndirs = 0;
  uint32_t j = 0;
  if (d->settings.hidden) {
    if (d->settings.dirfirst) {
      /* first pass: directories */
      for (uint32_t i = 0; i < d->length_all; i++) {
        if (file_isdir(d->files_all[i])) {
          d->files_sorted[j++] = d->files_all[i];
        }
      }
      ndirs = j;
      /* second pass: files */
      for (uint32_t i = 0; i < d->length_all; i++) {
        if (!file_isdir(d->files_all[i])) {
          d->files_sorted[j++] = d->files_all[i];
        }
      }
    } else {
      j = d->length_all;
      memcpy(d->files_sorted, d->files, d->length_all * sizeof *d->files_all);
    }
  } else {
    if (d->settings.dirfirst) {
      for (uint32_t i = 0; i < d->length_all; i++) {
        if (!file_hidden(d->files_all[i]) && file_isdir(d->files_all[i])) {
          d->files_sorted[j++] = d->files_all[i];
        }
      }
      ndirs = j;
      for (uint32_t i = 0; i < d->length_all; i++) {
        if (!file_hidden(d->files_all[i]) && !file_isdir(d->files_all[i])) {
          d->files_sorted[j++] = d->files_all[i];
        }
      }
    } else {
      for (uint32_t i = 0; i < d->length_all; i++) {
        if (!file_hidden(d->files_all[i])) {
          d->files_sorted[j++] = d->files_all[i];
        }
      }
    }
  }
  d->length_sorted = j;
  d->length = j;
  if (d->settings.reverse) {
    for (uint32_t i = 0; i < ndirs / 2; i++) {
      swap(d->files_sorted + i, d->files_sorted + ndirs - i - 1);
    }
    for (uint32_t i = 0; i < (d->length_sorted - ndirs) / 2; i++) {
      swap(d->files_sorted + ndirs + i,
           d->files_sorted + d->length_sorted - i - 1);
    }
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

  dir->length_all = vec_file_size(&files);
  dir->length_sorted = dir->length_all;
  dir->length = dir->length_all;

  vec_file_shrink_to_fit(&files);
  dir->files_all = files.data;
  dir->files_sorted = xmalloc(dir->length_all * sizeof *dir->files_all);
  dir->files = xmalloc(dir->length_all * sizeof *dir->files_all);

  memcpy(dir->files_sorted, dir->files_all,
         dir->length_all * sizeof *dir->files_all);
  memcpy(dir->files, dir->files_all, dir->length_all * sizeof *dir->files_all);
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
        for (int i = 0; i < head.level; i++) {
          file->name.str -= 2;
          while (*(file->name.str - 1) != '/') {
            file->name.str--;
          }
        }

        vec_file_push(&files, file);
      }
    }
    closedir(dirp);
  }
  queue_dirs_drop(&queue);

  dir->length_all = vec_file_size(&files);
  dir->length_sorted = dir->length_all;
  dir->length = dir->length_all;

  vec_file_shrink_to_fit(&files);
  dir->files_all = files.data;
  dir->files_sorted = xmalloc(dir->length_all * sizeof *dir->files_sorted);
  dir->files = xmalloc(dir->length_all * sizeof *dir->files);

  memcpy(dir->files_sorted, dir->files_all,
         dir->length_all * sizeof *dir->files_all);
  memcpy(dir->files, dir->files_all, dir->length_all * sizeof *dir->files_all);

  return dir;
}

void dir_cursor_move(Dir *d, int32_t ct, uint32_t height, uint32_t scrolloff) {
  d->ind = max(min(d->ind + ct, d->length - 1), 0);
  if (ct < 0) {
    d->pos = min(max(scrolloff, d->pos + ct), d->ind);
  } else {
    d->pos = max(min(height - 1 - scrolloff, d->pos + ct),
                 height - d->length + d->ind);
  }
  d->dirty = true;
}

static inline void dir_cursor_move_to_sel(Dir *d, uint32_t height,
                                          uint32_t scrolloff) {
  if (cstr_is_empty(&d->sel) || !d->files) {
    return;
  }

  for (uint32_t i = 0; i < d->length; i++) {
    if (cstr_equals_zv(&d->sel, file_name(d->files[i]))) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      goto cleanup;
    }
  }
  d->ind = min(d->ind, d->length);

cleanup:
  cstr_drop(&d->sel);
  d->sel = cstr_init();
}

void dir_cursor_move_to(Dir *d, zsview name, uint32_t height,
                        uint32_t scrolloff) {
  if (zsview_is_empty(name)) {
    return;
  }

  if (!d->files) {
    cstr_assign_zv(&d->sel, name);
    return;
  }

  for (uint32_t i = 0; i < d->length; i++) {
    if (zsview_eq(file_name(d->files[i]), &name)) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      return;
    }
  }
  d->ind = min(d->ind, d->length);
}

void dir_update_with(Dir *d, Dir *update, uint32_t height, uint32_t scrolloff) {
  if (cstr_is_empty(&d->sel) && d->ind < d->length) {
    const zsview *name = file_name(d->files[d->ind]);
    cstr_assign_zv(&d->sel, *name);
  }

  for (uint32_t i = 0; i < d->length_all; i++) {
    file_destroy(d->files_all[i]);
  }
  xfree(d->files_all);
  xfree(d->files_sorted);
  xfree(d->files);

  d->files_all = update->files_all;
  d->files_sorted = update->files_sorted;
  d->files = update->files;
  d->length_all = update->length_all;
  d->length_sorted = update->length_sorted;
  d->length = update->length;
  d->load_time = update->load_time;
  d->error = update->error;
  d->flatten_level = update->flatten_level;
  d->stat = update->stat;
  d->status = DIR_LOADING_FULLY;

  cstr_drop(&update->path);
  cstr_drop(&update->sel);
  xfree(update);

  d->sorted = false;
  dir_sort(d);

  if (!cstr_is_empty(&d->sel)) {
    dir_cursor_move_to_sel(d, height, scrolloff);
  }
  d->loading = false;
}

void dir_destroy(Dir *d) {
  if (!d) {
    return;
  }

  for (uint32_t i = 0; i < d->length_all; i++) {
    file_destroy(d->files_all[i]);
  }
  xfree(d->files_all);
  filter_destroy(d->filter);
  xfree(d->files_sorted);
  xfree(d->files);
  cstr_drop(&d->sel);
  cstr_drop(&d->path);
  hmap_cstr_drop(&d->tags.tags);
  xfree(d);
}
