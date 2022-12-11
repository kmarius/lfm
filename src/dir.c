#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cvector.h"
#include "dir.h"
#include "log.h"
#include "memory.h"
#include "sort.h"
#include "util.h"

File *dir_current_file(const Dir *d)
{
  if (!d || d->ind >= d->length) {
    return NULL;
  }

  return d->files[d->ind];
}


const char *dir_parent_path(const Dir *d)
{
  if (dir_isroot(d)) {
    return NULL;
  }

  static char tmp[PATH_MAX + 1];
  strcpy(tmp, d->path);
  return dirname(tmp);
}


static void apply_filter(Dir *d)
{
  if (d->filter) {
    uint32_t j = 0;
    for (uint32_t i = 0; i < d->length_sorted; i++) {
      if (filter_match(d->filter, file_name(d->files_sorted[i]))) {
        d->files[j++] = d->files_sorted[i];
      }
    }
    d->length = j;
  } else {
    /* TODO: try to select previously selected file
     * note that on the first call dir->files is not yet valid */
    memcpy(d->files, d->files_sorted, d->length_sorted * sizeof *d->files);
    d->length = d->length_sorted;
  }
  d->ind = max(min(d->ind, d->length - 1), 0);
}


static inline void swap(File **a, File **b)
{
  File *Dir = *a;
  *a = *b;
  *b = Dir;
}


/* sort allfiles and copy non-hidden ones to sortedfiles */
void dir_sort(Dir *d)
{
  if (!d->sorted) {
    switch (d->settings.sorttype) {
      case SORT_NATURAL:
        qsort(d->files_all, d->length_all, sizeof *d->files_all, compare_natural);
        break;
      case SORT_NAME:
        qsort(d->files_all, d->length_all, sizeof *d->files_all, compare_name);
        break;
      case SORT_SIZE:
        qsort(d->files_all, d->length_all, sizeof *d->files_all, compare_size);
        break;
      case SORT_CTIME:
        qsort(d->files_all, d->length_all, sizeof *d->files_all, compare_ctime);
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
      swap(d->files_sorted+i, d->files_sorted + ndirs - i - 1);
    }
    for (uint32_t i = 0; i < (d->length_sorted - ndirs) / 2; i++) {
      swap(d->files_sorted + ndirs + i, d->files_sorted + d->length_sorted - i - 1);
    }
  }

  apply_filter(d);
}


void dir_filter(Dir *d, const char *filter)
{
  if (d->filter) {
    filter_destroy(d->filter);
    d->filter = NULL;
  }

  if (filter && filter[0] != 0) {
    d->filter = filter_create(filter);
  }

  apply_filter(d);
}


bool dir_check(const Dir *d)
{
  struct stat statbuf;
  if (stat(d->path, &statbuf) == -1) {
    log_error("stat: %s", strerror(errno));
    return false;
  }
  return statbuf.st_mtime <= d->load_time;
}


Dir *dir_create(const char *path)
{
  Dir *d = xcalloc(1, sizeof *d);

  if (path[0] != '/') {
    char buf[PATH_MAX + 1];
    realpath(path, buf);
    d->path = strdup(buf);
  } else {
    /* to preserve symlinks we don't use realpath */
    d->path = strdup(path);
  }

  d->load_time = time(NULL);
  d->name = basename(d->path);
  d->next = current_millis();

  return d;
}


Dir *dir_load(const char *path, bool load_dircount)
{
  struct dirent *dp;
  Dir *dir = dir_create(path);
  dir->dircounts = load_dircount;

  DIR *dirp = opendir(path);
  if (!dirp) {
    log_error("opendir: %s", strerror(errno));
    dir->error = errno;
    return dir;
  }

  while ((dp = readdir(dirp))) {
    if (dp->d_name[0] == '.' &&
        (dp->d_name[1] == 0 ||
         (dp->d_name[1] == '.' && dp->d_name[2] == 0))) {
      continue;
    }

    File *file = file_create(path, dp->d_name);
    if (file) {
      if (load_dircount && file_isdir(file)) {
        file->dircount = file_dircount_load(file);
      }

      cvector_push_back(dir->files_all, file);
    }
  }
  closedir(dirp);

  dir->length_all = cvector_size(dir->files_all);
  dir->length_sorted = dir->length_all;
  dir->length = dir->length_all;

  dir->files_sorted = xmalloc(dir->length_all * sizeof *dir->files_all);
  dir->files = xmalloc(dir->length_all * sizeof *dir->files_all);

  memcpy(dir->files_sorted, dir->files_all, dir->length_all * sizeof *dir->files_all);
  memcpy(dir->files, dir->files_all, dir->length_all * sizeof *dir->files_all);
  dir->updates = 1;

  return dir;
}

struct queue_dirs_node {
  const char *path;
  uint32_t level;
  bool hidden;
  struct queue_dirs_node *next;
};

struct queue_dirs {
  struct queue_dirs_node *head;
  struct queue_dirs_node *tail;
};


Dir *dir_load_flat(const char *path, uint32_t level, bool load_dircount)
{
  Dir *dir = dir_create(path);
  dir->dircounts = load_dircount;
  dir->flatten_level = level;

  struct queue_dirs queue;
  queue.head = xmalloc(sizeof *queue.head);
  queue.head->path = path;
  queue.head->level = 0;
  queue.head->next = NULL;
  queue.head->hidden = false;

  while (queue.head) {
    struct queue_dirs_node *head = queue.head;
    queue.head = head->next;
    if (!queue.head) {
      queue.tail = NULL;
    }

    DIR *dirp = opendir(head->path);
    if (!dirp) {
      goto cont;
    }

    struct dirent *dp;
    while ((dp = readdir(dirp))) {
      if (dp->d_name[0] == '.' &&
          (dp->d_name[1] == 0 ||
           (dp->d_name[1] == '.' && dp->d_name[2] == 0))) {
        continue;
      }

      File *file = file_create(head->path, dp->d_name);
      if (file) {
        file->hidden |= head->hidden;
        if (file_isdir(file)) {
          if (load_dircount) {
            file->dircount = file_dircount_load(file);
          }

          if (head->level + 1 <= level) {
            struct queue_dirs_node *n = xmalloc(sizeof *n);
            n->path = file_path(file);
            n->level = head->level + 1;
            n->next = NULL;
            n->hidden = file_hidden(file);
            if (!queue.head) {
              queue.head = n;
              queue.tail = n;
            } else {
              queue.tail->next = n;
              queue.tail = n;
            }
          }
        }
        for (uint32_t i = 0; i < head->level; i++) {
          file->name -= 2;
          while (*(file->name-1) != '/') {
            file->name--;
          }
        }

        cvector_push_back(dir->files_all, file);
      }
    }
    closedir(dirp);
cont:
    xfree(head);
  }

  dir->length_all = cvector_size(dir->files_all);
  dir->length_sorted = dir->length_all;
  dir->length = dir->length_all;

  dir->files_sorted = xmalloc(dir->length_all * sizeof *dir->files_sorted);
  dir->files = xmalloc(dir->length_all * sizeof *dir->files);

  memcpy(dir->files_sorted, dir->files_all, dir->length_all * sizeof *dir->files_all);
  memcpy(dir->files, dir->files_all, dir->length_all * sizeof *dir->files_all);

  return dir;
}


void dir_cursor_move(Dir *d, int32_t ct, uint32_t height, uint32_t scrolloff)
{
  d->ind = max(min(d->ind + ct, d->length - 1), 0);
  if (ct < 0) {
    d->pos = min(max(scrolloff, d->pos + ct), d->ind);
  } else {
    d->pos = max(min(height - 1 - scrolloff, d->pos + ct), height - d->length + d->ind);
  }
}


static inline void dir_cursor_move_to_sel(Dir *d, uint32_t height, uint32_t scrolloff)
{
  if (!d->sel || !d->files) {
    return;
  }

  for (uint32_t i = 0; i < d->length; i++) {
    if (streq(file_name(d->files[i]), d->sel)) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      goto cleanup;
    }
  }
  d->ind = min(d->ind, d->length);

cleanup:
  XFREE_CLEAR(d->sel);
}


void dir_cursor_move_to(Dir *d, const char *name, uint32_t height, uint32_t scrolloff)
{
  if (!name) {
    return;
  }

  if (!d->files) {
    xfree(d->sel);
    d->sel = strdup(name);
    return;
  }

  for (uint32_t i = 0; i < d->length; i++) {
    if (streq(file_name(d->files[i]), name)) {
      dir_cursor_move(d, i - d->ind, height, scrolloff);
      return;
    }
  }
  d->ind = min(d->ind, d->length);
}


void dir_update_with(Dir *d, Dir *update, uint32_t height, uint32_t scrolloff)
{
  if (!d->sel && d->ind < d->length) {
    d->sel = strdup(file_name(d->files[d->ind]));
  }

  cvector_ffree(d->files_all, file_destroy);
  xfree(d->files_sorted);
  xfree(d->files);

  d->files_all = update->files_all;
  d->files_sorted = update->files_sorted;
  d->files = update->files;
  d->length_all = update->length_all;
  d->load_time = update->load_time;
  d->error = update->error;
  d->flatten_level = update->flatten_level;

  d->updates++;

  xfree(update->sel);
  xfree(update->path);
  xfree(update);

  d->sorted = false;
  dir_sort(d);

  if (d->sel) {
    dir_cursor_move_to_sel(d, height, scrolloff);
  }
}


void dir_destroy(Dir *d)
{
  if (!d) {
    return;
  }

  cvector_ffree(d->files_all, file_destroy);
  filter_destroy(d->filter);
  xfree(d->files_sorted);
  xfree(d->files);
  xfree(d->sel);
  xfree(d->path);
  xfree(d);
}
