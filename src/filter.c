#include <lauxlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "fuzzy.h"
#include "lua.h"
#include "lua/lfmlua.h"
#include "memory.h"
#include "util.h"

#define FILTER_INITIAL_CAPACITY 2

typedef struct Filter {
  bool (*match)(const Filter *, const File *file);
  void (*destroy)(Filter *);
  char *string;
  char *type;
  __compar_fn_t cmp;
} Filter;

bool filter_match(const Filter *filter, const File *file) {
  return filter->match(filter, file);
}

void filter_destroy(Filter *filter) {
  if (filter) {
    xfree(filter->string);
    filter->destroy(filter);
  }
}

const char *filter_string(const Filter *filter) {
  return filter ? filter->string : NULL;
}

const char *filter_type(const Filter *filter) {
  return filter ? filter->type : NULL;
}

__compar_fn_t filter_sort(const Filter *filter) {
  return filter->cmp;
}

// substring filtering

// Performance is surprisingly good even on very large directories (100k-1m
// files). Unless something changes don't bother with incrementally extending
// and existing filter and just rebuild it every time.

struct subfilter;

typedef struct SubstringFilter {
  Filter super;
  char *buf;
  uint32_t length;
  uint32_t capacity;
  struct subfilter *filters;
} SubstringFilter;

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
    s->atoms[s->length++].string = tok;
  }
}

bool sub_match(const Filter *filter, const File *file);
void sub_destroy(Filter *filter);

Filter *filter_create_sub(const char *filter) {
  if (filter[0] == 0) {
    return NULL;
  }

  SubstringFilter *f = xcalloc(1, sizeof *f);
  f->super.match = &sub_match;
  f->super.destroy = &sub_destroy;
  f->super.string = strdup(filter);
  f->super.type = FILTER_TYPE_SUBSTRING;

  f->capacity = FILTER_INITIAL_CAPACITY;
  f->filters = xmalloc(f->capacity * sizeof *f->filters);
  f->length = 0;

  f->buf = strdup(filter);
  for (char *tok = strtok(f->buf, " "); tok != NULL; tok = strtok(NULL, " ")) {
    if (f->length == f->capacity) {
      f->capacity *= 2;
      f->filters = xrealloc(f->filters, f->capacity * sizeof *f->filters);
    }
    subfilter_init(&f->filters[f->length++], tok);
  }

  return (Filter *)f;
}

void sub_destroy(Filter *filter) {
  SubstringFilter *f = (SubstringFilter *)filter;
  for (uint32_t i = 0; i < f->length; i++) {
    xfree(f->filters[i].atoms);
  }
  xfree(f->filters);
  xfree(f->buf);
  xfree(f);
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

bool sub_match(const Filter *filter, const File *file) {
  const SubstringFilter *f = (SubstringFilter *)filter;
  for (uint32_t i = 0; i < f->length; i++) {
    if (!subfilter_match(&f->filters[i], file->name)) {
      return false;
    }
  }
  return true;
}

// Fuzzy

typedef struct FuzzyFilter {
  Filter super;
} FuzzyFilter;

bool fuzzy_match(const Filter *filter, const File *file);
void fuzzy_destroy(Filter *filter);
static int cmpchoice(const void *_idx1, const void *_idx2);

Filter *filter_create_fuzzy(const char *filter) {
  FuzzyFilter *f = xcalloc(1, sizeof *f);
  f->super.match = &fuzzy_match;
  f->super.destroy = &fuzzy_destroy;
  f->super.cmp = cmpchoice;
  f->super.string = strdup(filter);
  f->super.type = FILTER_TYPE_FUZZY;
  return (Filter *)f;
}

bool fuzzy_match(const Filter *filter, const File *file) {
  if (fzy_has_match(filter->string, file->name)) {
    File *f = (File *)file;
    f->score = fzy_match(filter->string, file->name);
    return true;
  }
  return false;
}

void fuzzy_destroy(Filter *filter) {
  (void)filter;
}

static int cmpchoice(const void *_idx1, const void *_idx2) {
  const File *a = *(File **)_idx1;
  const File *b = *(File **)_idx2;

  if (a->score == b->score) {
    // TODO:
    /* To ensure a stable sort, we must also sort by the string
     * pointers. We can do this since we know all the strings are
     * from a contiguous memory segment (buffer in choices_t).
     */
    /* if (a->str < b->str) { */
    if (_idx1 < _idx2) {
      return -1;
    } else {
      return 1;
    }
  } else if (a->score < b->score) {
    return 1;
  } else {
    return -1;
  }
}

// Lua

typedef struct LuaFilter {
  Filter super;
  lua_State *L;
  int ref;
} LuaFilter;

bool lua_match(const Filter *filter, const File *file);
void lua_destroy(Filter *filter);

Filter *filter_create_lua(int ref, void *L) {
  LuaFilter *f = xcalloc(1, sizeof *f);
  f->super.match = &lua_match;
  f->super.destroy = &lua_destroy;
  f->super.string = strdup(FILTER_TYPE_LUA);
  f->super.type = FILTER_TYPE_LUA;

  f->L = L;
  f->ref = ref;
  return (Filter *)f;
}

bool lua_match(const Filter *filter, const File *file) {
  LuaFilter *f = (LuaFilter *)filter;
  return llua_filter(f->L, f->ref, file->name);
}

void lua_destroy(Filter *filter) {
  LuaFilter *f = (LuaFilter *)filter;
  luaL_unref(f->L, LUA_REGISTRYINDEX, f->ref);
}
