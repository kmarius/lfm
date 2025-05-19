#include "filter.h"

#include "fuzzy.h"
#include "lua/lfmlua.h"
#include "memory.h"
#include "util.h"

#include <lauxlib.h>
#include <lua.h>

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Filter {
  bool (*match)(const Filter *, const File *file);
  void (*destroy)(Filter *);
  char *string;
  const char *type;
  __compar_fn_t cmp;
} Filter;

bool filter_match(const Filter *filter, const File *file) {
  return filter->match(filter, file);
}

void filter_destroy(Filter *filter) {
  if (filter) {
    xfree(filter->string);
    filter->destroy(filter);
    xfree(filter);
  }
}

const char *filter_string(const Filter *filter) {
  return filter ? filter->string : NULL;
}

const char *filter_type(const Filter *filter) {
  return filter ? filter->type : NULL;
}

__compar_fn_t filter_cmp(const Filter *filter) {
  return filter->cmp;
}

// general filtering

struct subfilter {
  uint32_t length;
  struct filter_atom *atoms;
};

typedef struct SubstringFilter {
  Filter super;
  char *buf;
  uint32_t length;
  struct subfilter filters[];
} SubstringFilter;

struct filter_atom {
  bool (*pred)(const struct filter_atom *, const File *);
  union {
    int64_t size;
    void *string;
  };
  bool negate;
};

static bool pred_substr(const struct filter_atom *atom, const File *file) {
  return (strcasestr(file->name, atom->string) != NULL);
}

static bool pred_size_lt(const struct filter_atom *atom, const File *file) {
  return file->lstat.st_size < atom->size;
}

static bool pred_size_gt(const struct filter_atom *atom, const File *file) {
  return file->lstat.st_size > atom->size;
}

static bool pred_size_eq(const struct filter_atom *atom, const File *file) {
  return file->lstat.st_size == atom->size;
}

static inline int size_atom(struct filter_atom *atom, const char *tok) {
  assert(tok[0] == 's');
  if (tok[1] != '>' && tok[1] != '<' && tok[1] != '=') {
    return 1;
  }
  const char *start = &tok[2];
  if (tok[1] == '>') {
    if (tok[2] == '=') {
      atom->pred = pred_size_lt;
      atom->negate ^= 1;
      start++;
    } else {
      atom->pred = pred_size_gt;
    }
  } else if (tok[1] == '<') {
    if (tok[2] == '=') {
      atom->pred = pred_size_gt;
      atom->negate ^= 1;
      start++;
    } else {
      atom->pred = pred_size_lt;
    }
  } else {
    atom->pred = pred_size_eq;
  }
  char *end;
  double size = strtod(start, &end);
  if (end != start) {
    switch (tolower(end[0])) {
    case 'g':
      size *= 1024;
      // fall through
    case 'm':
      size *= 1024;
      // fall through
    case 'k':
      size *= 1024;
      // fall through
    case 0:
      break;
    default:
      // invalid char
      return 1;
    }
    if (end[0] && end[1]) {
      // trailing char
      return 1;
    }
    atom->size = (int64_t)size;
  }

  return 0;
}

static inline int charcnt(const char *s, char c) {
  int ct = 0;
  while (*s) {
    if (*s++ == c) {
      ct++;
    }
  }
  return ct;
}

static void subfilter_init(struct subfilter *s, char *filter) {
  s->length = 0;
  s->atoms = xmalloc((charcnt(filter, '|') + 1) * sizeof *s->atoms);

  for (char *ptr, *tok = strtok_r(filter, "|", &ptr); tok != NULL;
       tok = strtok_r(NULL, "|", &ptr)) {
    struct filter_atom *atom = &s->atoms[s->length];
    atom->negate = tok[0] == '!';
    if (tok[0] == '!') {
      tok++;
    }
    if (tok[0] == 0) {
      continue;
    }

    int ret = 1;
    switch (tok[0]) {
    case 's':
      ret = size_atom(atom, tok);
      break;
    }

    // fall back to literal string matching
    if (ret != 0) {
      atom->pred = pred_substr;
      atom->string = tok;
    }
    s->length++;
  }
  if (!s->length) {
    xfree(s->atoms);
  }
}

bool sub_match(const Filter *filter, const File *file);
void sub_destroy(Filter *filter);

Filter *filter_create_sub(const char *filter) {
  if (filter[0] == 0) {
    return NULL;
  }

  int num_subfilters = (charcnt(filter, ' ') + 1);

  SubstringFilter *f =
      xcalloc(1, num_subfilters * sizeof(struct subfilter) + sizeof *f);
  f->super.match = &sub_match;
  f->super.destroy = &sub_destroy;
  f->super.string = strdup(filter);
  f->super.type = FILTER_TYPE_GENERAL;

  f->length = 0;

  f->buf = strdup(filter);
  for (char *tok = strtok(f->buf, " "); tok != NULL; tok = strtok(NULL, " ")) {
    subfilter_init(&f->filters[f->length], tok);
    if (f->filters[f->length].length) {
      f->length++;
    }
  }

  return (Filter *)f;
}

void sub_destroy(Filter *filter) {
  SubstringFilter *f = (SubstringFilter *)filter;
  for (uint32_t i = 0; i < f->length; i++) {
    xfree(f->filters[i].atoms);
  }
  xfree(f->buf);
}

static inline bool atom_match(const struct filter_atom *a, const File *file) {
  return a->pred(a, file) != a->negate;
}

static inline bool subfilter_match(const struct subfilter *s,
                                   const File *file) {
  for (uint32_t i = 0; i < s->length; i++) {
    if (atom_match(&s->atoms[i], file)) {
      return true;
    }
  }
  return false;
}

bool sub_match(const Filter *filter, const File *file) {
  const SubstringFilter *f = (SubstringFilter *)filter;
  for (uint32_t i = 0; i < f->length; i++) {
    if (!subfilter_match(&f->filters[i], file)) {
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
