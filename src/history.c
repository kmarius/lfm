#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>

#include "cvector.h"
#include "history.h"
#include "util.h"

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */
/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: limit history size (on 2021-10-24) */

struct history_entry {
  char *line;
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
    struct history_entry n = { .line = line, .is_new = 0, };
    cvector_push_back(h->vec, n);
    line = NULL;
  }
  free(line);

  fclose(fp);
}


void history_write(History *h, const char *path)
{
  char *dir, *buf = strdup(path);
  dir = dirname(buf);
  mkdir_p(dir, 755);
  free(buf);

  FILE *fp = fopen(path, "a");
  if (!fp) {
    return;
  }

  for (size_t i = 0; i < cvector_size(h->vec); i++) {
    if (h->vec[i].is_new) {
      fputs(h->vec[i].line, fp);
      fputc('\n', fp);
    }
  }
  fclose(fp);
}


void history_deinit(History *h)
{
  for (size_t i = 0; i < cvector_size(h->vec); i++) {
    free(h->vec[i].line);
  }
  cvector_free(h->vec);
}


void history_append(History *h, const char *line)
{
  struct history_entry *end = cvector_end(h->vec);
  if (end && streq((end - 1)->line, line)) {
    return; /* skip consecutive dupes */
  }
  cvector_push_back(h->vec, ((struct history_entry) {strdup(line), true}));
}


void history_reset(History *h)
{
  h->ptr = NULL;
}


/* TODO: only show history items with matching prefixes (on 2021-07-24) */
const char *history_prev(History *h)
{
  if (!h->vec) {
    return NULL;
  }

  if (!h->ptr) {
    h->ptr = cvector_end(h->vec);
  }

  if (h->ptr > cvector_begin(h->vec)) {
    --h->ptr;
  }

  return h->ptr->line;
}


const char *history_next(History *h)
{
  if (!h->vec || !h->ptr) {
    return NULL;
  }

  if (h->ptr < cvector_end(h->vec)) {
    ++h->ptr;
  }

  /* TODO: could return the initial line here (on 2021-11-07) */
  if (h->ptr == cvector_end(h->vec)) {
    return "";
  }

  return h->ptr->line;
}
