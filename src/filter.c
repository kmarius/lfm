#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "util.h"

#define FILTER_INITIAL_CAPACITY 2

#define T Filter

// Performance is surprisingly good even on very large directories (100k-1m files).
// Unless something changes don't bother with incrementally extending and existing
// filter and just rebuild it every time.

struct subfilter;

typedef struct Filter {
  char *string;
  uint16_t length;
  uint16_t capacity;
  struct subfilter *filters;
} Filter;

struct filter_atom {
  char *string;
  bool negate;
};

struct subfilter {
  uint16_t length;
  uint16_t capacity;
  struct filter_atom *atoms;
};

static void subfilter_init(struct subfilter *s, char *filter)
{
  s->length = 0;
  s->capacity = FILTER_INITIAL_CAPACITY;
  s->atoms = malloc(s->capacity * sizeof *s->atoms);

  char *ptr;
  for (char *tok = strtok_r(filter, "|", &ptr);
      tok != NULL;
      tok = strtok_r(NULL, "|", &ptr)) {
    if (s->capacity == s->length) {
      s->capacity *= 2;
      s->atoms = realloc(s->atoms, s->capacity * sizeof *s->atoms);
    }
    s->atoms[s->length].negate = tok[0] == '!';
    if (s->atoms[s->length].negate)
      tok++;
    s->atoms[s->length++].string = strdup(tok);
  }
}


T *filter_create(const char *filter)
{
  T *t = malloc(sizeof *t);
  t->capacity = FILTER_INITIAL_CAPACITY;
  t->filters = malloc(t->capacity * sizeof *t->filters);
  t->length = 0;
  t->string = strdup(filter);

  char *buf = strdup(filter);
  for (char *tok = strtok(buf, " ");
      tok != NULL;
      tok = strtok(NULL, " ")) {
    if (t->length == t->capacity) {
      t->capacity *= 2;
      t->filters = realloc(t->filters, t->capacity * sizeof *t->filters);
    }
    subfilter_init(&t->filters[t->length++], tok);
  }

  free(buf);
  return t;
}


void filter_destroy(T *t)
{
  if (!t)
    return;

  for (uint16_t i = 0; i < t->length; i++) {
    for (uint16_t j = 0; j < t->filters[i].length; j++)
      free(t->filters[i].atoms[j].string);
    free(t->filters[i].atoms);
  }
  free(t->string);
  free(t->filters);
  free(t);
}


const char *filter_string(const T *t)
{
  return t ? t->string : "";
}


static inline bool atom_match(const struct filter_atom *a, const char *str)
{
  return (strcasestr(str, a->string) != NULL) != a->negate;
}


static inline bool subfilter_match(const struct subfilter *s, const char *str)
{
  for (uint16_t i = 0; i < s->length; i++)
    if (atom_match(&s->atoms[i], str))
      return true;
  return false;
}


bool filter_match(const T *t, const char *str)
{
  for (uint16_t i = 0; i < t->length; i++)
    if (!subfilter_match(&t->filters[i], str))
      return false;
  return true;
}
