#include "history.h"

#include "log.h"
#include "memory.h"
#include "path.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <libgen.h>

#define i_declared
#define i_type _history_list
#define i_key struct history_entry
#define i_no_clone
#define i_keydrop(p) free(p->prefix)
#define i_noclone
#include "stc/dlist.h"

#define i_declared
#define i_type _history_hmap
#define i_key const char *
#define i_val _history_list_node *
#define i_hash cstr_raw_hash
#define i_eq(p, q) (!strcmp(*(p), *(q)))
#include "stc/hmap.h"

/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: only show history items with matching prefixes (on 2021-07-24) */

// returns true on append, false on move to back
static inline bool append_or_move(History *self, struct history_entry e) {
  struct history_entry *entry = _history_list_push_back(&self->list, e);
  _history_hmap_iter it = _history_hmap_find(&self->map, e.line);
  if (it.ref) {
    _history_list_erase_node(&self->list, it.ref->second);
    it.ref->first = (char *)entry->line;
    it.ref->second = _history_list_get_node(entry);
  } else {
    _history_hmap_insert(&self->map, (char *)entry->line,
                         _history_list_get_node(entry));
  }
  return it.ref == NULL;
}

// don't call this on loaded history.
void history_load(History *h, const char *path) {
  memset(h, 0, sizeof *h);

  FILE *fp = fopen(path, "r");
  if (!fp) {
    return;
  }

  ssize_t read;
  size_t n;
  char *line = NULL;

  while ((read = getline(&line, &n, fp)) != -1) {
    if (line[read - 1] == '\n') {
      line[read - 1] = 0;
    }
    char *tab = strchr(line, '\t');
    if (!tab) {
      log_error("missing tab in history item: %s", line);
      xfree(line);
      line = NULL;
      continue;
    }
    *tab = 0;

    append_or_move(h, (struct history_entry){
                          .prefix = line,
                          .line = tab + 1,
                      });

    line = NULL;
  }
  xfree(line);

  fclose(fp);
  log_trace("%d history entries loaded", _history_hmap_size(&h->map));
  assert(_history_list_count(&h->list) == _history_hmap_size(&h->map));
}

void history_write(History *h, const char *path, int histsize) {
  char *dir = dirname_a(path);
  mkdir_p(dir, 755);
  xfree(dir);

  char *path_new;
  asprintf(&path_new, "%s.XXXXXX", path);
  int fd = mkstemp(path_new);
  if (fd < 0) {
    log_error("mkstemp: %s", strerror(errno));
    goto cleanup;
  }
  FILE *fp_new = fdopen(fd, "w");
  if (!fp_new) {
    log_error("fdopen: %s", strerror(errno));
    goto cleanup;
  }

  // We we read the history again here because another instace might have saved
  // its history since we loaded ours.

  const int num_keep_old = histsize - h->num_new_entries;

  int num_lines_written = 0;

  if (num_keep_old > 0) {
    FILE *fp_old = fopen(path, "r");
    if (fp_old) {
      int num_old_lines = 0;
      int c;
      while ((c = fgetc(fp_old)) != EOF) {
        if (c == '\n') {
          num_old_lines++;
        }
      }
      rewind(fp_old);
      const int num_skip_old = num_old_lines - num_keep_old;
      int i = 0;
      while (i < num_skip_old && (c = fgetc(fp_old)) != EOF) {
        if (c == '\n') {
          i++;
        }
      }
      while ((c = fgetc(fp_old)) != EOF) {
        fputc(c, fp_new);
      }
      fclose(fp_old);
      num_lines_written =
          num_old_lines > num_keep_old ? num_keep_old : num_old_lines;
    }
  }

  // new file now contains num_keep_old lines from the existing file if it was
  // positive, or nothing

  int num_save_new = histsize - num_lines_written;

  if (num_save_new > 0) {
    int num_skip_new = h->num_new_entries - num_save_new;

    _history_list_iter it = _history_list_begin(&h->list);

    // skip some of our new entries if we have more than histsize
    for (; it.ref != _history_list_back(&h->list) && num_skip_new > 0;
         _history_list_next(&it)) {
      struct history_entry *e = it.ref;
      if (e->is_new) {
        num_skip_new--;
      }
    }

    // write our new entries to path_new
    for (; it.ref; _history_list_next(&it)) {
      struct history_entry *e = it.ref;
      if (!e->is_new) {
        continue;
      }
      fputs(e->prefix, fp_new);
      fputc('\t', fp_new);
      fputs(e->line, fp_new);
      fputc('\n', fp_new);
    }
  }
  fclose(fp_new);

  rename(path_new, path);

cleanup:
  xfree(path_new);
}

void history_deinit(History *h) {
  _history_list_drop(&h->list);
}

void history_append(History *h, const char *prefix, const char *line) {
  _history_hmap_iter it = _history_hmap_find(&h->map, line);
  if (it.ref) {
    if (it.ref->second->value.is_new) {
      h->num_new_entries--;
    }
    _history_list_erase_node(&h->list, it.ref->second);
    _history_hmap_erase_at(&h->map, it);
  }

  int prefix_len = strlen(prefix);
  char *prefix_ = xmalloc(prefix_len + strlen(line) + 2);
  char *line_ = prefix_ + prefix_len + 1;
  bool is_new = line[0] != ' ';
  strcpy(prefix_, prefix);
  strcpy(prefix_ + prefix_len + 1, line);

  struct history_entry *entry =
      _history_list_push_back(&h->list, (struct history_entry){
                                            .is_new = is_new,
                                            .prefix = prefix_,
                                            .line = line_,
                                        });
  _history_hmap_insert(&h->map, (char *)line, _history_list_get_node(entry));

  if (is_new) {
    h->num_new_entries++;
  }

  history_reset(h);
}

void history_reset(History *h) {
  h->cur.ref = NULL;
}

const char *history_prev(History *h) {
  if (!h->cur.ref) {
    h->cur = _history_list_last(&h->list);
  } else {
    if (h->cur.ref != _history_list_front(&h->list)) {
      _history_list_prev(&h->cur);
    }
  }

  if (!h->cur.ref) {
    return NULL;
  }

  return h->cur.ref->line;
}

const char *history_next_entry(History *h) {
  if (!h->cur.ref) {
    return NULL;
  }

  _history_list_next(&h->cur);

  if (!h->cur.ref) {
    return NULL;
  }

  return h->cur.ref->line;
}

size_t history_size(History *self) {
  return _history_hmap_size(&self->map);
}

history_iter history_begin(History *self) {
  return _history_list_begin(&self->list);
}

void history_next(history_iter *it) {
  _history_list_next(it);
}
