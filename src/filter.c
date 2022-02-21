#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"
#include "util.h"
#include "log.h"

#define FILTER_INITIAL_CAPACITY 4

struct Subfilter;

typedef struct Filter {
	char *string;
	struct Subfilter *filters;
	uint16_t length;
	uint16_t capacity;
} Filter;

struct Subfilter {
	bool negate;
	char *filter;
};


#define T Filter


T *filter_create(const char *filter)
{
	T *t = malloc(sizeof(T));
	t->capacity = FILTER_INITIAL_CAPACITY;
	t->filters = malloc(t->capacity * sizeof(struct Subfilter));
	t->length = 0;
	t->string = strdup(filter);

	char *buf = strdup(filter);
	char *tok = strtok(buf, " ");
	for (; tok != NULL; tok = strtok(NULL, " ")) {
		if (t->length == t->capacity) {
			t->capacity *= 2;
			t->filters = realloc(t->filters, t->capacity * sizeof(struct Subfilter));
		}
		t->filters[t->length].negate = tok[0] == '!';
		if (t->filters[t->length].negate)
			tok++;
		t->filters[t->length].filter = strdup(tok);
		t->length++;
	}

	free(buf);
	return t;
}


void filter_destroy(T *t)
{
	if (!t)
		return;
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

	return (strcasestr(str, s->filter) != NULL) != s->negate;
}


bool filter_match(T *t, const char *str)
{
	for (uint16_t i = 0; i < t->length; i++)
		if (!match(&t->filters[i], str))
			return false;
	return true;
}
