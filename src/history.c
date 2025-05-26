#include "history.h"

#include "log.h"
#include "memory.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <libgen.h>

struct history_entry_raw {
  zsview prefix; // small string optimized, this is fine
  zsview line;
  bool is_new; // true if this item is new and not previously read from the
               // history file
};

#define i_declared
#define i_type _history_list
#define i_key struct history_entry
#define i_keyraw struct history_entry_raw
#define i_keyfrom(p)                                                           \
  ((struct history_entry){cstr_from_zv((p).prefix), cstr_from_zv((p).line),    \
                          (p).is_new})
#define i_keytoraw(p)                                                          \
  ((struct history_entry_raw){cstr_zv(&(p)->prefix), cstr_zv(&(p)->line),      \
                              (p)->is_new})
#define i_no_clone
#define i_keydrop(p) (cstr_drop(&(p)->line), cstr_drop(&(p)->prefix))
#include "stc/dlist.h"

// note that we don't emplace entries, raw entries are never converted
// this map doesn't own the cstr keys and no i_keydrop is needed
#define i_declared
#define i_type _history_hmap
#define i_key cstr
#define i_keyraw zsview
#define i_keyfrom cstr_from_zv
#define i_keytoraw cstr_zv
#define i_val _history_list_node *
#define i_hash zsview_hash
#define i_eq zsview_eq
#define i_no_clone
#include "stc/hmap.h"

/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: only show history items with matching prefixes (on 2021-07-24) */

// returns true on append (i.e. it is a new entry), false on move to back
static inline bool append_or_move(History *self, zsview prefix, zsview line) {
  // we aways append first and then remove the old entry, if it exists
  _history_hmap_value *v = _history_hmap_get_mut(&self->map, line);
  if (v) {
    // move to back
    _history_list_unlink_node(&self->list, v->second);
    _history_list_push_back_node(&self->list, v->second);
  } else {
    // construct and append new entry
    struct history_entry *entry = _history_list_emplace_back(
        &self->list, (struct history_entry_raw){
                         .prefix = prefix, .line = line, .is_new = true});
    _history_hmap_insert(&self->map, entry->line,
                         _history_list_get_node(entry));
  }
  return v == NULL;
}

// don't call this on loaded history.
void history_load(History *h, zsview path) {
  memset(h, 0, sizeof *h);

  FILE *fp = fopen(path.str, "r");
  if (fp == NULL) {
    return;
  }

  ssize_t read;
  size_t n;
  char *buf = NULL;

  while ((read = getline(&buf, &n, fp)) != -1) {
    if (buf[read - 1] == '\n') {
      buf[--read] = 0;
    }
    char *sep = strchr(buf, '\t');
    if (sep == NULL) {
      log_error("missing tab in history item: %s", buf);
      continue;
    }
    int pos = sep - buf;

    zsview prefix = zsview_from_n(buf, pos);
    zsview line = zsview_from_n(sep + 1, read - pos - 1);

    if (zsview_is_empty(prefix) || zsview_is_empty(line)) {
      log_error("missing prefix or line in history item: %s", buf);
      continue;
    }

    append_or_move(h, prefix, line);
  }
  xfree(buf);

  fclose(fp);
  log_trace("%d history entries loaded", _history_hmap_size(&h->map));
  assert(_history_list_count(&h->list) == _history_hmap_size(&h->map));
}

void history_write(History *h, zsview path, int histsize) {
  make_dirs(path, 755);

  char path_new[PATH_MAX + 1];
  snprintf(path_new, sizeof path_new - 1, "%s.XXXXXX", path.str);
  int fd = mkstemp(path_new);
  if (fd < 0) {
    log_error("mkstemp: %s", strerror(errno));
    return;
  }
  FILE *fp_new = fdopen(fd, "w");
  if (fp_new == NULL) {
    log_error("fdopen: %s", strerror(errno));
    return;
  }

  // We we read the history again here because another instace might have saved
  // its history since we loaded ours.

  const int num_keep_old = histsize - h->num_new_entries;

  int num_lines_written = 0;

  if (num_keep_old > 0) {
    FILE *fp_old = fopen(path.str, "r");
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
      fwrite(cstr_str(&e->prefix), 1, cstr_size(&e->prefix), fp_new);
      fputc('\t', fp_new);
      fwrite(cstr_str(&e->line), 1, cstr_size(&e->line), fp_new);
      fputc('\n', fp_new);
    }
  }
  fclose(fp_new);

  rename(path_new, path.str);
}

void history_deinit(History *h) {
  _history_list_drop(&h->list);
  _history_hmap_drop(&h->map);
}

void history_append(History *h, zsview prefix, zsview line) {
  if (zsview_is_empty(prefix) || zsview_is_empty(line)) {
    return;
  }

  if (append_or_move(h, prefix, line)) {
    h->num_new_entries++;
  }

  // reset cursor
  history_reset(h);
}

void history_reset(History *h) {
  h->cur.ref = NULL;
}

zsview history_prev(History *h) {
  if (h->cur.ref == NULL) {
    h->cur = _history_list_last(&h->list);
  } else {
    if (h->cur.ref != _history_list_front(&h->list)) {
      _history_list_prev(&h->cur);
    }
  }

  if (h->cur.ref == NULL) {
    return c_zv("");
  }

  return cstr_zv(&h->cur.ref->line);
}

zsview history_next_entry(History *h) {
  if (h->cur.ref == NULL) {
    return c_zv("");
  }

  _history_list_next(&h->cur);

  if (h->cur.ref == NULL) {
    return c_zv("");
  }

  return cstr_zv(&h->cur.ref->line);
}

size_t history_size(const History *self) {
  return _history_hmap_size(&self->map);
}

history_iter history_begin(const History *self) {
  return _history_list_begin(&self->list);
}

void history_next(history_iter *it) {
  _history_list_next(it);
}
