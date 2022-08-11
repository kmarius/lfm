#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"
#include "hashtab.h"

#define EXT_CHANNEL_TAB_SIZE 128  // size of the hashtable mapping extensions to color channels

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

// automatically generated, see config/pathdefs.c.in
extern char *default_data_dir;
extern char *default_lua_dir;

typedef struct Config {
  char *configdir;      // ~/.config/lfm
  char *configpath;     // ~/.config/lfm/init.lua
  char *user_datadir;   // ~/.local/share/lfm
  char *historypath;    // ~/.local/share/lfm/history
  char *datadir;        // /usr/share/lfm
  char *luadir;         // /usr/share/lfm/lua
  char *corepath;       // /usr/share/lfm/lua/core.lua
  char *rundir;         // $XDG_RUNTIME_DIR or /tmp/runtime-$USER
  char *fifopath;       // rundir/$PID.fifo
  char *logpath;        // /tmp/lfm.$PID.log
  wchar_t truncatechar; // '~'
  char *lastdir;
  char *selfile;
  char *startpath;
  char *startfile;
  bool preview;
  char *previewer;
  bool hidden;
  uint32_t scrolloff;
  cvector_vector_type(char *) commands;
  cvector_vector_type(uint32_t) ratios;
  cvector_vector_type(char *) inotify_blacklist;
  uint32_t inotify_timeout;
  uint32_t inotify_delay;

  struct colors {
    Hashtab ext;  // char* -> uint64

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

void config_ratios_set(cvector_vector_type(uint32_t) ratios);

void config_ext_channel_add(const char *ext, uint64_t channel);

void config_colors_clear();
