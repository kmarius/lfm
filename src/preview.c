#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "dir.h"
#include "log.h"
#include "popen_arr.h"
#include "preview.h"

static inline Preview *preview_init(Preview *pv, const char *path, uint8_t nrow)
{
	pv->path = strdup(path);
	pv->lines = NULL;
	pv->mtime = 0;
	pv->nrow = nrow;
	pv->loading = false;
	return pv;
}

static inline Preview *preview_create(const char *path, uint8_t nrow)
{
	return preview_init(malloc(sizeof(Preview)), path, nrow);
}

Preview *preview_create_loading(const char *path, uint8_t nrow)
{
	Preview *pv = preview_create(path, nrow);
	pv->loading = true;
	return pv;
}

Preview *preview_create_from_file(const char *path, uint8_t nrow)
{
	char buf[4096];

	Preview *pv = preview_create(path, nrow);

	if (cfg.previewer == NULL) {
		return pv;
	}

	/* TODO: redirect stderr? (on 2021-08-10) */
	char *const args[3] = {cfg.previewer, (char*) path, NULL};
	FILE *fp = popen_arr(cfg.previewer, args, false);
	if (fp == NULL) {
		log_error("preview: %s", strerror(errno));
		return pv;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL && nrow > 0) {
		cvector_push_back(pv->lines, strdup(buf));
	}
	pv->nrow -= nrow;

	pclose(fp);

	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		pv->mtime = 0;
	} else {
		pv->mtime = statbuf.st_mtime;
	}

	return pv;
}

static inline void preview_deinit(Preview *pv)
{
	if (pv == NULL) {
		return;
	}
	cvector_ffree(pv->lines, free);
	free(pv->path);
}

void preview_destroy(Preview *pv)
{
	preview_deinit(pv);
	free(pv);
}
