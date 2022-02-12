#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"

#define NCCHANNEL_INITIALIZER_PALINDEX(ind) \
	(ind < 0 \
	 ? ~NC_BGDEFAULT_MASK & 0xff000000lu \
	 : (((NC_BGDEFAULT_MASK | NC_BG_PALETTE) & 0xff000000lu) | (ind & 0xff)))

#define NCCHANNEL_INITIALIZER_HEX(hex) \
	(hex < 0 \
	 ? ~NC_BGDEFAULT_MASK & 0xff000000lu \
	 : ((NC_BGDEFAULT_MASK & 0xff000000lu) | (hex & 0xffffff)))

#define NCCHANNELS_INITIALIZER_PALINDEX(fg, bg) \
	((NCCHANNEL_INITIALIZER_PALINDEX(fg) << 32lu) \
	 | NCCHANNEL_INITIALIZER_PALINDEX(bg))

typedef struct ext_channel_tup {
	char *ext;
	uint64_t channel;
} ext_channel_tup;

// automatically generated, see config/pathdefs.c.in
extern char *default_data_dir;
extern char *default_lua_dir;

typedef struct Config {
	wchar_t truncatechar; /* '~' */
	char *corepath;       /* ~/.local/share/lfm/lua/core.lua */
	char *historypath;    /* ~/.local/share/lfm/history */
	char *configpath;     /* ~/.config/lfm/config.lua */
	char *configdir;      /* ~/.config/lfm */
	char *user_datadir;        /* ~/.local/share/lfm */
	char *datadir;        /* /usr/share/lfm */
	char *luadir;        /* /usr/share/lfm/lua */
	char *fifopath;       /* $rundir/$PID.fifo */
	char *logpath;        /* /tmp/lfm.$PID.log */
	char *rundir;         /* /run/media/user/N/lfm */
	char *lastdir;
	char *selfile;
	char *startpath;
	char *startfile;
	char *previewer;
	bool hidden;
	bool preview;
	uint8_t scrolloff;
	cvector_vector_type(char *) commands;
	cvector_vector_type(char *) inotify_blacklist;
	cvector_vector_type(uint16_t) ratios;

	struct colors {
		ext_channel_tup *ext_channels;

		uint64_t normal;
		uint64_t selection;
		uint64_t copy;
		uint64_t delete;
		uint64_t search;
		uint64_t broken;
		uint64_t exec;
		uint64_t dir;
		uint32_t current; /* bg channel index only */
	} colors;
} Config;

extern Config cfg;

void config_init();

void config_deinit();

void config_ratios_set(cvector_vector_type(uint16_t) ratios);

void config_ext_channel_add(const char *ext, uint64_t channel);

void config_colors_clear();
