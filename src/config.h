#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"

typedef struct Config {
	wchar_t truncatechar;
	char *corepath;    /* ~/.local/share/lfm/lua/core.lua */
	char *historypath; /* ~/.local/share/lfm/history */
	char *configpath;  /* ~/.config/lfm/config.lua */
	char *configdir;   /* ~/.config/lfm */
	char *datadir;     /* ~/.local/share/lfm */
	char *fifopath;    /* $rundir/$PID.fifo */
	char *rundir;      /* /run/media/user/N/lfm */
	char *lastdir;
	char *selfile;
	char *startpath;
	char *previewer;
	bool hidden;
	bool preview;
	cvector_vector_type(char*) commands;
	cvector_vector_type(int) ratios;
	int scrolloff;
} config;

extern config cfg;

void config_ratios_set(size_t n, const int *ratios);

void config_defaults();

void config_clear();

#endif
