#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"
#include "hashtab.h"

typedef struct config_s {
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
  bool preview_images;
  Hashtab *image_extensions;
  bool icons;
  Hashtab *icon_map;
  char *previewer;
  bool hidden;
  uint32_t scrolloff;
  cvector_vector_type(char *) commands;
  cvector_vector_type(uint32_t) ratios;
  cvector_vector_type(char *) inotify_blacklist;
  uint32_t inotify_timeout;
  uint32_t inotify_delay;

  struct colors {
    Hashtab *ext;  // char* -> uint64

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

static inline void image_extension_add(const char *ext)
{
  char *val = strdup(ext);
  ht_set(cfg.image_extensions, val, val);
}

static inline void config_icon_map_add(const char *ext, const char *icon)
{
  const size_t l = strlen(icon);
  char *val = malloc(l + strlen(ext) + 2);
  strcpy(val, icon);
  strcpy(val + l + 1, ext);
  ht_set(cfg.icon_map, val + l + 1, val);
}
