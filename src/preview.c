#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "dir.h"
#include "log.h"
#include "popen_arr.h"
#include "preview.h"

preview_t *preview_new(const char *path, int nrow)
{
	preview_t *pv = malloc(sizeof(preview_t));
	pv->path = strdup(path);
	pv->lines = NULL;
	pv->mtime = 0;
	pv->nrow = nrow;
	pv->loading = false;
	return pv;
}

preview_t *preview_new_loading(const char *path, int nrow)
{
	preview_t *pv = preview_new(path, nrow);
	pv->loading = true;
	/* cvector_push_back(pv->lines, strdup("loading")); */
	return pv;
}

/* line length currently not limited */
preview_t *preview_new_from_file(const char *path, int nrow)
{
	preview_t *pv = preview_new(path, nrow);

	FILE *fp;
	char buf[4096];

	if (cfg.previewer != NULL) {
		char *const args[3] = {cfg.previewer, (char*) path, NULL};
		/* TODO: redirect stderr (on 2021-08-10) */
		if ((fp = popen_arr(cfg.previewer, args, false)) == NULL) {
			log_error("preview: %s", strerror(errno));
			return pv;
		}

		for (int i = 0; fgets(buf, sizeof(buf), fp) != NULL && i < nrow; i++) {
			cvector_push_back(pv->lines, strdup(buf));
		}

		pclose(fp);
	}

	struct stat statbuf;
	if (stat(path, &statbuf) == -1) {
		pv->mtime = 0;
	} else {
		pv->mtime = statbuf.st_mtime;
	}

	return pv;
}

void preview_free(preview_t *pv)
{
	if (pv != NULL) {
		cvector_ffree(pv->lines, free);
		free(pv->path);
		free(pv);
	}
}
