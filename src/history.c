#include "history.h"

#include "cvector.h"
#include "hashtab.h"
#include "log.h"
#include "memory.h"
#include "path.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <libgen.h>

/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: only show history items with matching prefixes (on 2021-07-24) */

void free_history_entry(void *p) {
  if (p) {
    struct history_entry *e = p;
    free(e->prefix);
    free(e);
  }
}

// don't call this on loaded history.
void history_load(History *h, const char *path) {
  memset(h, 0, sizeof *h);

  lht_init(&h->items, HT_DEFAULT_CAPACITY, free_history_entry);

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
    char *hist_line = tab + 1;

    struct history_entry *e = xmalloc(sizeof *e);
    e->prefix = line;
    e->line = hist_line;
    e->is_new = 0;

    // make sure duplicates are stored at the last occurrence
    lht_delete(&h->items, hist_line);
    lht_set(&h->items, hist_line, e);

    line = NULL;
  }
  xfree(line);

  fclose(fp);
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

    struct lht_bucket *iter = h->items.first;

    // skip some of our new entries if we have more than histsize
    for (; iter != h->items.last && num_skip_new > 0; iter = iter->order_next) {
      struct history_entry *e = iter->val;
      if (e->is_new) {
        num_skip_new--;
      }
    }

    // write our new entries to path_new
    for (; iter; iter = iter->order_next) {
      struct history_entry *e = iter->val;
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
  lht_deinit(&h->items);
}

void history_append(History *h, const char *prefix, const char *line) {
  struct history_entry *old = lht_get(&h->items, line);
  if (old) {
    if (old->is_new) {
      h->num_new_entries--;
    }
    lht_delete(&h->items, line);
  }

  int prefix_len = strlen(prefix);
  struct history_entry *e = xmalloc(sizeof *e);
  e->is_new = line[0] != ' ';
  e->prefix = xmalloc(prefix_len + strlen(line) + 2);
  e->line = e->prefix + prefix_len + 1;
  strcpy(e->prefix, prefix);
  strcpy(e->prefix + prefix_len + 1, line);

  lht_set(&h->items, line, e);
  if (e->is_new) {
    h->num_new_entries++;
  }

  history_reset(h);
}

void history_reset(History *h) {
  h->cur = NULL;
}

const char *history_prev(History *h) {
  if (!h->cur) {
    h->cur = h->items.last;
  } else {
    if (h->cur != h->items.first) {
      h->cur = h->cur->order_prev;
    }
  }

  if (!h->cur) {
    return NULL;
  }

  struct history_entry *e = h->cur->val;
  return e->line;
}

const char *history_next(History *h) {
  if (!h->cur) {
    return NULL;
  }

  h->cur = h->cur->order_next;

  if (!h->cur) {
    return NULL;
  }

  struct history_entry *e = h->cur->val;
  return e->line;
}
