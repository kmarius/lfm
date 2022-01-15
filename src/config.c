#define _GNU_SOURCE
#include <linux/limits.h>
#include <ncurses.h> // COLOR_ constants
#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config.h"
#include "cvector.h"
#include "log.h"

config cfg = {
	.truncatechar = L'~',
	.preview = true,
	.scrolloff = 4,
	.colors = {
		.ext_channels = NULL,
		.normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1),
		.copy = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
		.current = NCCHANNEL_INITIALIZER_PALINDEX(237),
		.delete = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_RED),
		.dir = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLUE, -1),
		.broken = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_RED, -1),
		.exec = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_GREEN, -1),
		.search = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
		.selection = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_MAGENTA),
	}
};

void config_ratios_set(cvector_vector_type(uint16_t) ratios)
{
	if (cvector_size(ratios) > 0) {
		cvector_free(cfg.ratios);
		cfg.ratios = ratios;
	}
}

void config_ext_channel_add(const char *ext, uint64_t channel)
{
	/* TODO: should overwrite existing tuples or something (on 2022-01-14) */
	cvector_push_back(cfg.colors.ext_channels, ((ext_channel_tup) {strdup(ext), channel}));
}

void config_init()
{
	cvector_vector_type(uint16_t) r = NULL;
	cvector_push_back(r, 1);
	cvector_push_back(r, 2);
	cvector_push_back(r, 3);
	config_ratios_set(r);

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

#define tup_free(t) free((t).ext)

void config_deinit() {
	cvector_free(cfg.ratios);
	cvector_free(cfg.commands);
	cvector_free(cfg.inotify_blacklist);
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
	cvector_ffree(cfg.colors.ext_channels, tup_free);
}

#undef tup_free
