#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <linux/limits.h>

#include "config.h"
#include "cvector.h"
#include "log.h"

config cfg = {
    .truncatechar = L'~',
	.fifopath = NULL,
	.rundir = NULL,
    .corepath = NULL,
	.configdir = NULL,
	.datadir = NULL,
	.configpath = NULL,
    .historypath = NULL,
    .startpath = NULL,
    .previewer = NULL,
    /* TODO: is hidden the wrong way around? (on 2021-07-22) */
    .hidden = false, /* show hidden files */
    .lastdir = NULL,
    .selfile = NULL,
    .ratios = NULL,
    .preview = true,
	.commands = NULL,
	.scrolloff = 4,
};

void config_ratios_set(size_t n, const int *ratios)
{
	size_t i;
	if (n > 0) {
		cvector_set_size(cfg.ratios, 0);
		for (i = 0; i < n; i++) {
			cvector_push_back(cfg.ratios, ratios[i]);
		}
	}
}

void config_defaults()
{
	const int r[] = {1, 2, 3};
	config_ratios_set(3, r);

	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s/.config/lfm", getenv("HOME"));
	cfg.configdir = strdup(buf);

	snprintf(buf, sizeof(buf), "%s/.local/share/lfm", getenv("HOME"));
	cfg.datadir = strdup(buf);

	snprintf(buf, sizeof(buf), "%s/config.lua", cfg.configdir);
	cfg.configpath = strdup(buf);

	snprintf(buf, sizeof(buf), "%s/history", cfg.datadir);
	cfg.historypath = strdup(buf);

	snprintf(buf, sizeof(buf), "%s/lua/core.lua", cfg.datadir);
	cfg.corepath = strdup(buf);
}

void config_clear() {
	cvector_free(cfg.ratios);
	cvector_free(cfg.commands);
	free(cfg.configdir);
	free(cfg.datadir);
	free(cfg.previewer);
	free(cfg.corepath);
	free(cfg.configpath);
	free(cfg.historypath);
}
