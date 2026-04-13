#include "history.h"

#include "log.h"
#include "memory.h"
#include "stcutil.h"
#include "util.h"

#include <stc/cstr.h>

#include <errno.h>
#include <stdio.h>

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
int history_load(History *h, zsview path, const char *lockpath) {
  memset(h, 0, sizeof *h);

  int rc = 0;
  char *buf = NULL;
  int lock = -1;
  FILE *fp = NULL;

  if (lockpath) {
    lock = acquire_file_lock(lockpath, LOCK_TIMEOUT_MS);
    if (lock < 0) {
      log_error("could not acquire lock: %s", lockpath);
      goto err;
    }
  }

  fp = fopen(path.str, "r");
  if (unlikely(fp == NULL)) {
    log_error("fopen: %s", strerror(errno));
    goto err;
  }

  isize read;
  usize n;
  while ((read = getline(&buf, &n, fp)) != -1) {
    if (buf[read - 1] == '\n')
      buf[--read] = 0;

    char *sep = strchr(buf, '\t');
    if (unlikely(sep == NULL)) {
      log_error("malformed history item: %s", buf);
      continue;
    }
    i32 pos = sep - buf;

    zsview prefix = zsview_from_n(buf, pos);
    zsview line = zsview_from_n(sep + 1, read - pos - 1);

    if (unlikely(zsview_is_empty(prefix) || zsview_is_empty(line))) {
      log_error("malformed history item: %s", buf);
      continue;
    }

    append_or_move(h, prefix, line, false);
  }
  if (ferror(fp)) {
    log_error("getline: %s", strerror(errno));
    goto err;
  }

  assert(_history_list_count(&h->list) == _history_hmap_size(&h->map));

out:
  if (fp && fclose(fp) != 0) {
    log_error("fclose: %s", strerror(errno));
    rc = -1;
  }
  release_file_lock(lock);
  xfree(buf);

  return rc;

err:
  rc = -1;
  goto out;
}

static inline int write_line(zsview prefix, zsview line, FILE *file) {
  if (fwrite(prefix.str, 1, prefix.size, file) != (usize)prefix.size) {
    perror("fwrite");
    return -1;
  }
  if (fputc('\t', file) == EOF) {
    perror("fputc");
    return -1;
  }
  if (fwrite(line.str, 1, line.size, file) != (usize)line.size) {
    perror("fwrite");
    return -1;
  }
  if (fputc('\n', file) == EOF) {
    perror("fputc");
    return -1;
  }
  return 0;
}

int history_write(History *h, zsview path, i32 histsize, const char *lockpath) {
  int rc = 0;
  History old = {0};

  if (make_dirs(path, 755) != 0) {
    log_error("mkdir: %s", strerror(errno));
    return -1;
  }

  char temp_path[PATH_MAX + 1];
  snprintf(temp_path, sizeof temp_path - 1, "%s.XXXXXX", path.str);

  FILE *fp = mkstempf(temp_path);
  if (fp == NULL)
    return -1; // err logged in mkstempf

  // We read the history again from disk and append our new entries. We use a
  // lock file in order not to overwrite changes from another instance. This
  // might be a problem with larger history sizes

  int lock = acquire_file_lock(lockpath, LOCK_TIMEOUT_MS);
  if (lock < 0)
    goto err;

  if (history_load(&old, path, NULL) != 0)
    goto err;

  // append our new entries
  c_foreach(it, _history_list, h->list) {
    if (it.ref->is_new)
      history_append(&old, cstr_zv(&it.ref->prefix), cstr_zv(&it.ref->line));
  }

  i32 skip = history_size(&old) - histsize;

  c_foreach(it, _history_list, old.list) {
    if (skip-- <= 0)
      rc = write_line(cstr_zv(&it.ref->prefix), cstr_zv(&it.ref->line), fp);
    if (rc != 0)
      break;
  }
  if (rc != 0)
    goto err; // err logged in write_line

  if (fclose(fp) != 0) {
    log_error("fclose: %s", strerror(errno));
    goto err;
  }
  fp = NULL;

  if (rename(temp_path, path.str) != 0) {
    log_error("rename: %s", strerror(errno));
    goto err;
  }

out:
  release_file_lock(lock);

  history_deinit(&old);
  return rc;

err:
  if (fp && fclose(fp) != 0)
    log_error("fclose: %s", strerror(errno));
  if (unlink(temp_path) != 0)
    log_error("unlink: %s", strerror(errno));

  rc = -1;
  goto out;
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
