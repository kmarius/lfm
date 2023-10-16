#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "memory.h"
#include "util.h"

#define FILTER_INITIAL_CAPACITY 2

// Performance is surprisingly good even on very large directories (100k-1m
// files). Unless something changes don't bother with incrementally extending
// and existing filter and just rebuild it every time.

struct subfilter;

typedef struct filter_s {
  char *string;
  uint32_t length;
  uint32_t capacity;
  struct subfilter *filters;
} Filter;

struct filter_atom {
  char *string;
  bool negate;
};

struct subfilter {
  uint32_t length;
  uint32_t capacity;
  struct filter_atom *atoms;
};

static void subfilter_init(struct subfilter *s, char *filter) {
  s->length = 0;
  s->capacity = FILTER_INITIAL_CAPACITY;
  s->atoms = xmalloc(s->capacity * sizeof *s->atoms);

  char *ptr;
  for (char *tok = strtok_r(filter, "|", &ptr); tok != NULL;
       tok = strtok_r(NULL, "|", &ptr)) {
    if (s->capacity == s->length) {
      s->capacity *= 2;
      s->atoms = xrealloc(s->atoms, s->capacity * sizeof *s->atoms);
    }
    s->atoms[s->length].negate = tok[0] == '!';
    if (s->atoms[s->length].negate) {
      tok++;
    }
    s->atoms[s->length++].string = strdup(tok);
  }
}

Filter *filter_create(const char *filter) {
  Filter *f = xmalloc(sizeof *f);
  f->capacity = FILTER_INITIAL_CAPACITY;
  f->filters = xmalloc(f->capacity * sizeof *f->filters);
  f->length = 0;
  f->string = strdup(filter);

  char *buf = strdup(filter);
  for (char *tok = strtok(buf, " "); tok != NULL; tok = strtok(NULL, " ")) {
    if (f->length == f->capacity) {
      f->capacity *= 2;
      f->filters = xrealloc(f->filters, f->capacity * sizeof *f->filters);
    }
    subfilter_init(&f->filters[f->length++], tok);
  }

  xfree(buf);
  return f;
}

void filter_destroy(Filter *s) {
  if (!s) {
    return;
  }

  for (uint32_t i = 0; i < s->length; i++) {
    for (uint32_t j = 0; j < s->filters[i].length; j++) {
      xfree(s->filters[i].atoms[j].string);
    }
    xfree(s->filters[i].atoms);
  }
  xfree(s->string);
  xfree(s->filters);
  xfree(s);
}

const char *filter_string(const Filter *f) {
  return f ? f->string : NULL;
}

static inline bool atom_match(const struct filter_atom *a, const char *str) {
  return (strcasestr(str, a->string) != NULL) != a->negate;
}

static inline bool subfilter_match(const struct subfilter *s, const char *str) {
  for (uint32_t i = 0; i < s->length; i++) {
    if (atom_match(&s->atoms[i], str)) {
      return true;
    }
  }
  return false;
}

bool filter_match(const Filter *f, const char *str) {
  for (uint32_t i = 0; i < f->length; i++) {
    if (!subfilter_match(&f->filters[i], str)) {
      return false;
    }
  }
  return true;
}
