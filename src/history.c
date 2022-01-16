#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>

#include "history.h"
#include "util.h"

#define T History

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */
/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: limit history size (on 2021-10-24) */

struct history_entry {
	char *line;
	bool is_new;
};

// don't call this on loaded history.
void history_load(T *t, const char *path)
{
	t->vec = NULL;
	t->ptr = NULL;

	FILE *fp = fopen(path, "r");
	if (!fp)
		return;

	ssize_t read;
	size_t n;
	char *line = NULL;

	while ((read = getline(&line, &n, fp)) != -1) {
		if (line[read-1] == '\n')
			line[read-1] = 0;
		struct history_entry n = { .line = line, .is_new = 0, };
		cvector_push_back(t->vec, n);
		line = NULL;
	}
	free(line);

	fclose(fp);
}

void history_write(T *t, const char *path)
{
	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	FILE *fp = fopen(path, "a");
	if (!fp)
		return;

	for (size_t i = 0; i < cvector_size(t->vec); i++) {
		if (t->vec[i].is_new) {
			fputs(t->vec[i].line, fp);
			fputc('\n', fp);
		}
	}
	fclose(fp);
}

void history_deinit(T *t)
{
	for (size_t i = 0; i < cvector_size(t->vec); i++)
		free(t->vec[i].line);

	cvector_free(t->vec);
}

void history_append(T *t, const char *line)
{
	struct history_entry *end = cvector_end(t->vec);
	if (end && streq((end - 1)->line, line))
		return; /* skip consecutive dupes */
	cvector_push_back(t->vec, ((struct history_entry) {strdup(line), true}));
}

void history_reset(T *t)
{
	t->ptr = NULL;
}

/* TODO: only show history items with matching prefixes (on 2021-07-24) */
const char *history_prev(T *t)
{
	if (!t->vec)
		return NULL;

	if (!t->ptr)
		t->ptr = cvector_end(t->vec);

	if (t->ptr > cvector_begin(t->vec))
		--t->ptr;

	return t->ptr->line;
}

const char *history_next(T *t)
{
	if (!t->vec || !t->ptr)
		return NULL;

	if (t->ptr < cvector_end(t->vec))
		++t->ptr;

	/* TODO: could return the initial line here (on 2021-11-07) */
	if (t->ptr == cvector_end(t->vec))
		return "";

	return t->ptr->line;
}

#undef T
