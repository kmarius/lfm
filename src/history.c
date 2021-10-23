#include <stdio.h>
#include <libgen.h>

#include "history.h"
#include "log.h"
#include "util.h"

#define T history_t

/* TODO: add prefixes to history (on 2021-07-24) */
/* TODO: write to history.new and move on success (on 2021-07-28) */
/* TODO: signal errors on load/write (on 2021-10-23) */

void history_load(T *t, const char *path)
{
	char *line = NULL;
	ssize_t read;
	size_t n;
	FILE *fp;

	t->vec = NULL;
	t->ptr = NULL;

	if (! (fp = fopen(path, "r"))) {
		/* app_error("history: %s", strerror(errno)); */
		return;
	}

	while ((read = getline(&line, &n, fp)) != -1) {
		line[strlen(line) - 1] = 0; /* remove \n */
		cvector_push_back(t->vec, line);
		line = NULL;
	}

	fclose(fp);
}

void history_write(T *t, const char *path)
{
	log_trace("history_write");
	FILE *fp;

	char *dir, *buf = strdup(path);
	dir = dirname(buf);
	mkdir_p(dir);
	free(buf);

	if (!(fp = fopen(path, "w"))) {
		/* ui_error(ui, "history: %s", strerror(errno)); */
		return;
	}

	size_t i;
	for (i = 0; i < cvector_size(t->vec); i++) {
		fputs(t->vec[i], fp);
		fputc('\n', fp);
	}
	fclose(fp);
}

void history_append(T *t, const char *line)
{
	char **end = cvector_end(t->vec);
	if (end && streq(*(end - 1), line)) {
		/* skip consecutive dupes */
		return;
	}
	cvector_push_back(t->vec, strdup(line));
}

void history_reset(T *t)
{
	t->ptr = NULL;
}

/* does *not* free the vector */
void history_clear(T *t)
{
	cvector_ffree(t->vec, free);
	t->vec = NULL;
	t->ptr = NULL;
}

/* TODO: only show history items with matching prefixes (on 2021-07-24) */
const char *history_prev(T *t)
{
	if (!t->ptr) {
		t->ptr = cvector_end(t->vec);
	}
	if (!t->ptr) {
		return NULL;
	}
	if (t->ptr > cvector_begin(t->vec)) {
		--t->ptr;
	}
	return *t->ptr;
}

const char *history_next(T *t)
{
	if (!t->vec) {
		return NULL;
	}
	if (t->ptr < cvector_end(t->vec)) {
		++t->ptr;
	}
	if (t->ptr == cvector_end(t->vec)) {
		return "";
	}
	return *t->ptr;
}

#undef T
