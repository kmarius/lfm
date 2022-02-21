#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "util.h"
#include "log.h"

#define FILTER_INITIAL_CAPACITY 2

/* TODO: test performance on large directories and check out if we can incrementally
 * build a new filter from the previous one or vice-versa (on 2022-02-21) */

#define T Filter

struct Subfilter;

typedef struct Filter {
	char *string;
	uint16_t length;
	uint16_t capacity;
	struct Subfilter *filters;
} Filter;

struct FilterAtom {
	bool negate;
	char *string;
};

struct Subfilter {
	uint16_t length;
	uint16_t capacity;
	struct FilterAtom *atoms;
};


static void subfilter_init(struct Subfilter *s, char *filter)
{
	s->length = 0;
	s->capacity = FILTER_INITIAL_CAPACITY;
	s->atoms = malloc(s->capacity * sizeof(struct FilterAtom));

	char *ptr;
	for (char *tok = strtok_r(filter, "|", &ptr);
			tok != NULL;
			tok = strtok_r(NULL, "|", &ptr)) {
		if (s->capacity == s->length) {
			s->capacity *= 2;
			s->atoms = realloc(s->atoms, s->capacity * sizeof(struct Subfilter));
		}
		s->atoms[s->length].negate = tok[0] == '!';
		if (s->atoms[s->length].negate)
			tok++;
		s->atoms[s->length++].string = strdup(tok);
	}
}


T *filter_create(const char *filter)
{
	T *t = malloc(sizeof(T));
	t->capacity = FILTER_INITIAL_CAPACITY;
	t->filters = malloc(t->capacity * sizeof(struct Subfilter));
	t->length = 0;
	t->string = strdup(filter);

	char *buf = strdup(filter);
	for (char *tok = strtok(buf, " ");
			tok != NULL;
			tok = strtok(NULL, " ")) {
		if (t->length == t->capacity) {
			t->capacity *= 2;
			t->filters = realloc(t->filters, t->capacity * sizeof(struct Subfilter));
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


const char *filter_string(T *t)
{
	if (!t)
		return "";
	return t->string;
}


static inline bool match(struct Subfilter *s, const char *str)
{
	for (uint16_t i = 0; i < s->length; i++)
		if ((strcasestr(str, s->atoms[i].string) != NULL) != s->atoms[i].negate)
			return true;
	return false;
}


bool filter_match(T *t, const char *str)
{
	for (uint16_t i = 0; i < t->length; i++)
		if (!match(&t->filters[i], str))
			return false;
	return true;
}
