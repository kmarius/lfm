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

  h->num_old_entries = cvector_size(h->entries);

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

  const int num_keep_old = histsize - h->num_new_entries;

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
    }
  }

  // skip some of our new entries if we have more than histsize
  size_t i = h->num_old_entries;
  if (num_keep_old < 0) {
    int num_skip_new = -num_keep_old;
    for (; i < cvector_size(h->entries) && num_skip_new > 0; i++) {
      if (h->entries[i].line[0] != ' ') {
        num_skip_new--;
      }
    }
  }

  // write our new entries to path_new
  for (; i < cvector_size(h->entries); i++) {
    if (h->entries[i].line[0] == ' ') {
      continue;
    }
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
  if (*line != ' ') {
    h->num_new_entries++;
  }
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
