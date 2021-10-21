#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <linux/limits.h>
#include <notcurses/notcurses.h>

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
	.logpath = NULL,
	.configpath = NULL,
	.historypath = NULL,
	.startpath = NULL,
	.startfile = NULL,
	.previewer = NULL,
	/* TODO: is hidden the wrong way around? (on 2021-07-22) */
	.hidden = false, /* show hidden files */
	.lastdir = NULL,
	.selfile = NULL,
	.ratios = NULL,
	.preview = true,
	.commands = NULL,
	.scrolloff = 4,
	.colors = {
		.ext_channels = NULL,
		.normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1),
		.copy = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
		.current = NCCHANNEL_INITIALIZER_PALINDEX(237),
		.delete = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_RED),
		.dir = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLUE, -1),
		.exec = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_GREEN, -1),
		.search = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
		.selection = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_PINK),
	}
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

void config_ext_channel_add(const char *ext, unsigned long channel)
{
	chtup_t t = { .ext = strdup(ext), .channel = channel };
	cvector_push_back(cfg.colors.ext_channels, t);
}

void config_defaults()
{
	const int r[] = {1, 2, 3};
	config_ratios_set(3, r);

#ifdef DEBUG
	cfg.logpath = strdup("/tmp/lfm.debug.log");
#else
	asprintf(&cfg.logpath, "/tmp/lfm.%d.log", getpid());
#endif

	asprintf(&cfg.rundir, "/var/run/user/%d/lfm", getuid());

#ifdef DEBUG
	asprintf(&cfg.fifopath, "%s/debug.fifo", cfg.rundir);
#else
	asprintf(&cfg.fifopath, "%s/%d.fifo", cfg.rundir, getpid());
#endif

	asprintf(&cfg.configdir, "%s/.config/lfm", getenv("HOME"));

	asprintf(&cfg.datadir, "%s/.local/share/lfm", getenv("HOME"));

	asprintf(&cfg.configpath, "%s/config.lua", cfg.configdir);

	asprintf(&cfg.historypath, "%s/history", cfg.datadir);

	asprintf(&cfg.corepath, "%s/lua/core.lua", cfg.datadir);
}

#define chtup_free(t) free((t).ext)

void config_clear() {
	cvector_free(cfg.ratios);
	cvector_free(cfg.commands);
	free(cfg.configdir);
	free(cfg.configpath);
	free(cfg.corepath);
	free(cfg.datadir);
	free(cfg.fifopath);
	free(cfg.historypath);
	free(cfg.logpath);
	free(cfg.previewer);
	free(cfg.startfile);
	free(cfg.startpath);
	cvector_ffree(cfg.colors.ext_channels, chtup_free);
}
