#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "popen_arr.h"
#include "preview.h"
#include "util.h"

#define T Preview

#define PREVIEW_INITIALIZER ((T) {})

#define PREVIEW_MAX_LINE_LENGTH 1024 // includes escapes and color codes

static inline T *preview_init(T *t, const char *path, uint8_t nrow)
{
	*t = PREVIEW_INITIALIZER;
	t->path = strdup(path);
	t->nrow = nrow;
	return t;
}


static inline T *preview_create(const char *path, uint8_t nrow)
{
	return preview_init(malloc(sizeof(T)), path, nrow);
}


static inline void preview_deinit(T *t)
{
	if (!t)
		return;

	cvector_ffree(t->lines, free);
	free(t->path);
}


void preview_destroy(T *t)
{
	preview_deinit(t);
	free(t);
}


T *preview_create_loading(const char *path, uint8_t nrow)
{
	T *t = preview_create(path, nrow);
	t->loading = true;
	return t;
}


void preview_update_with(T *t, Preview *update)
{
	cvector_ffree(t->lines, free);
	t->lines = update->lines;
	t->mtime = update->mtime;
	t->loadtime = update->loadtime;
	t->loading = false;

	free(update->path);
	free(update);
}


T *preview_create_from_file(const char *path, uint8_t nrow)
{
	char buf[PREVIEW_MAX_LINE_LENGTH];

	T *t = preview_create(path, nrow);
	t->loadtime = current_millis();

	struct stat statbuf;
	t->mtime = stat(path, &statbuf) != -1 ? statbuf.st_mtime : 0;

	if (!cfg.previewer)
		return t;

	/* TODO: redirect stderr? (on 2021-08-10) */
	char *const args[3] = {cfg.previewer, (char*) path, NULL};
	FILE *fp = popen_arr(cfg.previewer, args, false);
	if (!fp) {
		log_error("preview: %s", strerror(errno));
		return t;
	}

	while (fgets(buf, sizeof(buf), fp) && nrow > 0)
		cvector_push_back(t->lines, strdup(buf));

	pclose(fp);

	return t;
}
