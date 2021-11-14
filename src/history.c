#include <libgen.h>
#include <stdio.h>

#include "history.h"
#include "util.h"

#define T history_t

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */
/* TODO: signal errors on load/write (on 2021-10-23) */
/* TODO: limit history size (on 2021-10-24) */

struct history_node_t {
	char *line;
	int new;
};

void history_load(T *t, const char *path)
{
	char *line = NULL;
	ssize_t read;
	size_t n;
	FILE *fp;

	t->vec = NULL;
	t->ptr = NULL;

	if ((fp = fopen(path, "r")) == NULL) {
		return;
	}

	while ((read = getline(&line, &n, fp)) != -1) {
		if (line[read-1] == '\n')
			line[read-1] = 0;
		struct history_node_t n = { .line = line, .new = 0, };
		cvector_push_back(t->vec, n);
		line = NULL;
	}
	free(line);

	fclose(fp);
}

void history_write(T *t, const char *path)
{
	FILE *fp;
	size_t i;

	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	if ((fp = fopen(path, "a")) == NULL) {
		return;
	}

	for (i = 0; i < cvector_size(t->vec); i++) {
		if (t->vec[i].new) {
			fputs(t->vec[i].line, fp);
			fputc('\n', fp);
		}
	}
	fclose(fp);
}

void history_append(T *t, const char *line)
{
	struct history_node_t *end = cvector_end(t->vec);
	if (end && streq((end - 1)->line, line)) {
		/* skip consecutive dupes */
		return;
	}
	struct history_node_t n = { .line = strdup(line), .new = 1, };
	cvector_push_back(t->vec, n);
}

void history_reset(T *t)
{
	t->ptr = NULL;
}

/* does *not* free the vector */
void history_deinit(T *t)
{
	size_t i;

	for (i = 0; i < cvector_size(t->vec); i++) {
		free(t->vec[i].line);
	}
	cvector_free(t->vec);
}

/* TODO: only show history items with matching prefixes (on 2021-07-24) */
const char *history_prev(T *t)
{
	if (t->vec == NULL) {
		return NULL;
	}
	if (t->ptr == NULL) {
		t->ptr = cvector_end(t->vec);
	}
	if (t->ptr > cvector_begin(t->vec)) {
		--t->ptr;
	}
	return t->ptr->line;
}

const char *history_next(T *t)
{
	if (t->vec == NULL || t->ptr == NULL) {
		return NULL;
	}
	if (t->ptr < cvector_end(t->vec)) {
		++t->ptr;
	}
	if (t->ptr == cvector_end(t->vec)) {
		/* TODO: could return the initial line here (on 2021-11-07) */
		return "";
	}
	return t->ptr->line;
}

#undef T
