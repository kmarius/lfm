#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "dir.h"
#include "log.h"
#include "preview.h"
#include "popen_arr.h"

preview_t *new_preview(const char *path, const file_t *fptr, int nrow, int ncol)
{
	(void)nrow;
	(void)ncol;
	preview_t *pv = malloc(sizeof(preview_t));
	pv->fptr = fptr;
	pv->lines = NULL;
	pv->path = strdup(path);
	pv->mtime = 0;
	pv->access = 0;
	return pv;
}

preview_t *new_loading_preview(const char *path, const file_t *fptr, int nrow,
			       int ncol)
{
	preview_t *pv = new_preview(path, fptr, nrow, ncol);
	/* cvector_push_back(pv->lines, strdup("loading")); */
	return pv;
}

/* line length currently not limited */
preview_t *new_file_preview(const char *path, const file_t *fptr, int nrow, int ncol)
{
	preview_t *pv = new_preview(path, fptr, nrow, ncol);

	FILE *fp;
	char buf[4096];

	const char *args[3] = {cfg.previewer, path, NULL};
	/* TODO: redirect stderr (on 2021-08-10) */
	fp = popen_arr(cfg.previewer, args, false);
	if (!fp) {
		log_error("preview: %s", strerror(errno));
		return pv;
	}

	for (int i = 0; fgets(buf, sizeof(buf), fp) && i < nrow; i++) {
		cvector_push_back(pv->lines, strdup(buf));
	}

	pclose(fp);

	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		pv->mtime = 0;
	} else {
		pv->mtime = statbuf.st_mtime;
	}

	return pv;
}

bool preview_check(const preview_t *pv)
{
	struct stat statbuf;
	if (stat(pv->path, &statbuf) == -1) {
		// error, don't reload?
		/* TODO: or give *error* preview? (on 2021-08-02) */
		return true;
	}
	return pv->mtime >= statbuf.st_mtime;
}

void free_preview(preview_t *pv)
{
	if (pv) {
		cvector_ffree(pv->lines, free);
		free(pv->path);
		free(pv);
	}
}
