#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_PINK 5
#define COLOR_TEAL 6

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

typedef struct chtup_t {
	char *ext;
	unsigned long channel;
} chtup_t;

typedef struct Config {
	wchar_t truncatechar; /* '~' */
	char *corepath;       /* ~/.local/share/lfm/lua/core.lua */
	char *historypath;    /* ~/.local/share/lfm/history */
	char *configpath;     /* ~/.config/lfm/config.lua */
	char *configdir;      /* ~/.config/lfm */
	char *datadir;        /* ~/.local/share/lfm */
	char *fifopath;       /* $rundir/$PID.fifo */
	char *rundir;         /* /run/media/user/N/lfm */
	char *lastdir;
	char *selfile;
	char *startpath;
	char *startfile;
	char *previewer;
	bool hidden;
	bool preview;
	int scrolloff;
	cvector_vector_type(char*) commands;
	cvector_vector_type(int) ratios;

	struct colors {
		chtup_t *ext_channels;

		unsigned long normal;
		unsigned long selection;
		unsigned long copy;
		unsigned long delete;
		unsigned long search;
		unsigned long exec;
		unsigned long dir;
		unsigned current; /* bg channel index only */
	} colors;
} config;

extern config cfg;

void config_ratios_set(size_t n, const int *ratios);

void config_ext_channel_add(const char *ext, unsigned long channel);

void config_defaults();

void config_clear();

#endif
