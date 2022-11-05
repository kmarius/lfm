#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>

#include "cvector.h"
#include "history.h"
#include "util.h"
#include "log.h"

/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: only show history items with matching prefixes (on 2021-07-24) */

// prefix contains an allocated string, line points to right after its nul byte.
struct history_entry {
  char *prefix;
  const char *line;
  bool is_new;
};

// don't call this on loaded history.
void history_load(History *h, const char *path)
{
  memset(h, 0, sizeof *h);

  FILE *fp = fopen(path, "r");
  if (!fp) {
    return;
  }

  ssize_t read;
  size_t n;
  char *line = NULL;

  while ((read = getline(&line, &n, fp)) != -1) {
    if (line[read-1] == '\n') {
      line[read-1] = 0;
    }
    char *tab = strchr(line, '\t');
    if (!tab) {
      log_error("missing tab in history item: %s", line);
      xfree(line);
      line = NULL;
      continue;
    }
    *tab = 0;
    struct history_entry n = {
      .prefix = line,
      .line = tab + 1,
      .is_new = 0,
    };
    cvector_push_back(h->entries, n);
    line = NULL;
  }
  xfree(line);

  h->old_entries = cvector_size(h->entries);

  fclose(fp);
}

void history_write(History *h, const char *path, int histsize)
{
  char *dir = dirname_a(path);
  mkdir_p(dir, 755);
  xfree(dir);

  char *path_new;
  asprintf(&path_new, "%s.new", path);

  FILE *fp_new = fopen(path_new, "w");
  if (!fp_new) {
    goto cleanup;
  }

  // We we read the history again here because another instace might have saved
  // its history since we loaded ours.

  const int new_entries = cvector_size(h->entries) - h->old_entries;
  const int diff = histsize - new_entries;

  if (diff > 0) {
    // read up to diff from the tail of path

    char **lines = xcalloc(diff, sizeof *lines);
    int i = 0;

    FILE *fp_old = fopen(path, "r");
    if (fp_old) {
      size_t n;
      char *line = NULL;

      while (getline(&line, &n, fp_old) != -1) {
        xfree(lines[i % diff]);
        lines[i++ % diff] = line;
        line = NULL;
      }
      xfree(line);
      fclose(fp_old);
    }

    // write them to path_new
    if (i < diff) {
      for (int j = 0; j < i; j++) {
        fputs(lines[j], fp_new);
        xfree(lines[j]);
      }
    } else {
      for (int j = i; j < i + diff; j++) {
        fputs(lines[j % diff], fp_new);
        xfree(lines[j % diff]);
      }
    }

    xfree(lines);
  }

  // write our new entries to path_new
  size_t i = diff > 0 ? h->old_entries : cvector_size(h->entries) - histsize;
  for (; i < cvector_size(h->entries); i++) {
    fputs(h->entries[i].prefix, fp_new);
    fputc('\t', fp_new);
    fputs(h->entries[i].line, fp_new);
    fputc('\n', fp_new);
  }
  fclose(fp_new);

  rename(path_new, path);

cleanup:
  xfree(path_new);
}

void history_deinit(History *h)
{
  for (size_t i = 0; i < cvector_size(h->entries); i++) {
    xfree(h->entries[i].prefix);
  }
  cvector_free(h->entries);
}

void history_append(History *h, const char *prefix, const char *line)
{
  struct history_entry *end = cvector_end(h->entries);
  if (end && streq((end - 1)->line, line)) {
    return; /* skip consecutive dupes */
  }
  int l = strlen(prefix);
  struct history_entry e = {.is_new = true};
  e.prefix = xmalloc(l + strlen(line) + 2);
  e.line = e.prefix + l + 1;
  strcpy(e.prefix, prefix);
  strcpy(e.prefix + l + 1, line);
  cvector_push_back(h->entries, e);
}

void history_reset(History *h)
{
  h->cur = NULL;
}

const char *history_prev(History *h)
{
  if (!h->entries) {
    return NULL;
  }

  if (!h->cur) {
    h->cur = cvector_end(h->entries);
  }

  if (h->cur > cvector_begin(h->entries)) {
    --h->cur;
  }

  return h->cur->line;
}

const char *history_next(History *h)
{
  if (!h->entries || !h->cur) {
    return NULL;
  }

  if (h->cur < cvector_end(h->entries)) {
    ++h->cur;
  }

  /* TODO: could return the initial line here (on 2021-11-07) */
  if (h->cur == cvector_end(h->entries)) {
    return "";
  }

  return h->cur->line;
}
