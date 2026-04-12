#include "history.h"

#include "log.h"
#include "memory.h"
#include "stcutil.h"
#include "util.h"

#include <stc/cstr.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/limits.h>
#include <unistd.h>

#define LOCK_TIMEOUT_MS 250

struct history_entry_raw {
  zsview prefix; // small string optimized, this is fine
  zsview line;
  bool is_new; // true if this item is new and not previously read from the
               // history file, or old but moved in the history
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
#include <stc/dlist.h>

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
#include <stc/hmap.h>

/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: only show history items with matching prefixes (on 2021-07-24) */

// New entry is marked with `is_new`.
// Returns true if it is a new entry, or an old entry that was moved to
// the back for the first time.
static inline bool append_or_move(History *self, zsview prefix, zsview line,
                                  bool is_new) {
  struct history_entry *entry = NULL;
  _history_hmap_value *v = _history_hmap_get_mut(&self->map, line);
  if (v) {
    // move to back
    _history_list_unlink_node(&self->list, v->second);
    _history_list_push_back_node(&self->list, v->second);
    entry = &v->second->value;
  } else {
    // construct and append new entry
    struct history_entry_raw e = {
        .prefix = prefix,
        .line = line,
        .is_new = true,
    };
    entry = _history_list_emplace_back(&self->list, e);
    _history_hmap_insert(&self->map, entry->line,
                         _history_list_get_node(entry));
  }
  bool is_old = !entry->is_new;
  entry->is_new = is_new;
  return is_old;
}

// don't call this on loaded history.
void history_load(History *h, zsview path, const char *lockpath) {
  memset(h, 0, sizeof *h);

  int lock = -1;
  if (lockpath) {
    lock = acquire_file_lock(lockpath, LOCK_TIMEOUT_MS);
    if (lock < 0) {
      log_error("could not acquire lock: %s", lockpath);
      return;
    }
  }

  FILE *fp = fopen(path.str, "r");
  if (unlikely(fp == NULL)) {
    release_file_lock(lock);
    return;
  }

  isize read;
  usize n;
  char *buf = NULL;

  while ((read = getline(&buf, &n, fp)) != -1) {
    if (buf[read - 1] == '\n')
      buf[--read] = 0;

    char *sep = strchr(buf, '\t');
    if (unlikely(sep == NULL)) {
      log_error("missing tab in history item: %s", buf);
      continue;
    }
    i32 pos = sep - buf;

    zsview prefix = zsview_from_n(buf, pos);
    zsview line = zsview_from_n(sep + 1, read - pos - 1);

    if (unlikely(zsview_is_empty(prefix) || zsview_is_empty(line))) {
      log_error("missing prefix or line in history item: %s", buf);
      continue;
    }

    append_or_move(h, prefix, line, false);
  }
  xfree(buf);

  fclose(fp);
  release_file_lock(lock);
  log_trace("%d history entries loaded", _history_hmap_size(&h->map));
  assert(_history_list_count(&h->list) == _history_hmap_size(&h->map));
}

static inline void write_line(zsview prefix, zsview line, FILE *file) {
  fwrite(prefix.str, 1, prefix.size, file);
  fputc('\t', file);
  fwrite(line.str, 1, line.size, file);
  fputc('\n', file);
}

void history_write(History *h, zsview path, i32 histsize,
                   const char *lockpath) {
  make_dirs(path, 755);

  char path_new[PATH_MAX + 1];
  snprintf(path_new, sizeof path_new - 1, "%s.XXXXXX", path.str);

  int fd = mkstemp(path_new);
  if (fd < 0) {
    log_error("mkstemp: %s", strerror(errno));
    return;
  }
  FILE *fp = fdopen(fd, "w");
  if (fp == NULL) {
    log_error("fdopen: %s", strerror(errno));
    close(fd);
    return;
  }

  // We read the history again from disk and append our new entries. We use a
  // lock file in order not to overwrite changes from another instance. This
  // might be a problem with larger history sizes

  int lock = acquire_file_lock(lockpath, LOCK_TIMEOUT_MS);
  if (lock < 0) {
    fclose(fp);
    return;
  }

  History old;
  history_load(&old, path, NULL);

  // append our new entries
  c_foreach(it, _history_list, h->list) {
    if (it.ref->is_new)
      history_append(&old, cstr_zv(&it.ref->prefix), cstr_zv(&it.ref->line));
  }

  i32 skip = history_size(&old) - histsize;

  c_foreach(it, _history_list, old.list) {
    if (skip-- <= 0)
      write_line(cstr_zv(&it.ref->prefix), cstr_zv(&it.ref->line), fp);
  }
  fclose(fp);
  rename(path_new, path.str);
  release_file_lock(lock);

  history_deinit(&old);
}

void history_deinit(History *h) {
  _history_list_drop(&h->list);
  _history_hmap_drop(&h->map);
}

void history_append(History *h, zsview prefix, zsview line) {
  if (zsview_is_empty(prefix) || zsview_is_empty(line))
    return;

  if (append_or_move(h, prefix, line, true))
    h->num_new_entries++;

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
    if (h->cur.ref != _history_list_front(&h->list))
      _history_list_prev(&h->cur);
  }

  if (h->cur.ref == NULL)
    return c_zv("");

  return cstr_zv(&h->cur.ref->line);
}

zsview history_next_entry(History *h) {
  if (h->cur.ref == NULL)
    return c_zv("");

  _history_list_next(&h->cur);

  if (h->cur.ref == NULL)
    return c_zv("");

  return cstr_zv(&h->cur.ref->line);
}

usize history_size(const History *self) {
  return _history_hmap_size(&self->map);
}

history_iter history_begin(const History *self) {
  return _history_list_begin(&self->list);
}

void history_next(history_iter *it) {
  _history_list_next(it);
}
