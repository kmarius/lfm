#include <linux/limits.h>
#include <ncurses.h> // COLOR_ constants
#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "cvector.h"
#include "log.h"
#include "util.h"


Config cfg = {
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
	if (cvector_size(ratios) == 0)
		return;
	cvector_free(cfg.ratios);
	cfg.ratios = ratios;

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

	const char *rundir = getenv("XDG_RUNTIME_DIR");
	if (!rundir) {
		asprintf(&cfg.rundir, "/tmp/runtime-%s/lfm", getenv("USER"));
	} else {
		asprintf(&cfg.rundir, "%s/lfm", rundir);
	}

#ifdef DEBUG
	asprintf(&cfg.fifopath, "%s/debug.fifo", cfg.rundir);
#else
	asprintf(&cfg.fifopath, "%s/%d.fifo", cfg.rundir, getpid());
#endif

	asprintf(&cfg.configdir, "%s/.config/lfm", getenv("HOME"));

	asprintf(&cfg.user_datadir, "%s/.local/share/lfm", getenv("HOME"));

	cfg.datadir = strdup(default_data_dir);

	asprintf(&cfg.configpath, "%s/init.lua", cfg.configdir);

	asprintf(&cfg.historypath, "%s/history", cfg.user_datadir);

	cfg.luadir = strdup(default_lua_dir);

	asprintf(&cfg.corepath, "%s/lfm.lua", cfg.luadir);
}

#define tup_free(t) free((t).ext)

void config_deinit()
{
	cvector_free(cfg.ratios);
	cvector_free(cfg.commands);
	cvector_free(cfg.inotify_blacklist);
	free(cfg.configdir);
	free(cfg.configpath);
	free(cfg.corepath);
	free(cfg.user_datadir);
	free(cfg.datadir);
	free(cfg.fifopath);
	free(cfg.historypath);
	free(cfg.logpath);
	free(cfg.previewer);
	free(cfg.startfile);
	free(cfg.startpath);
	free(cfg.luadir);
	cvector_ffree(cfg.colors.ext_channels, tup_free);
}

void config_colors_clear()
{
	cfg.colors.ext_channels = NULL;
	cfg.colors.normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.copy = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.current = NCCHANNEL_INITIALIZER_PALINDEX(237);
	cfg.colors.delete = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.dir = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.broken = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.exec = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.search = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
	cfg.colors.selection = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);

	cvector_ffree(cfg.colors.ext_channels, tup_free);
}
