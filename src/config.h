#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <wchar.h>

#include "cvector.h"
#include "dir.h"
#include "hashtab.h"

typedef struct config_s {
  char *configdir;      // ~/.config/lfm
  char *configpath;     // ~/.config/lfm/init.lua
  char *statedir;       // ~/.local/state/lfm
  char *historypath;    // ~/.local/state/lfm/history
  char *datadir;        // /usr/share/lfm
  char *luadir;         // /usr/share/lfm/lua
  char *corepath;       // /usr/share/lfm/lua/core.lua
  char *rundir;         // $XDG_RUNTIME_DIR or /tmp/runtime-$USER
  char *cachedir;       // $XDG_CACHE_HOME/lfm or ~/.cache/lfm
  char *fifopath;       // rundir/$PID.fifo
  char *logpath;        // /tmp/lfm.$PID.log

  int histsize;         // 100
  wchar_t truncatechar; // '~'
  char *lastdir;
  char *selfile;
  char *startpath;
  char *startfile;
  bool preview;
  bool preview_images;
  char *previewer;
  bool icons;
  Hashtab *icon_map;
  uint32_t scrolloff;
  cvector_vector_type(char *) commands;
  cvector_vector_type(uint32_t) ratios;

  cvector_vector_type(char *) inotify_blacklist;
  uint32_t inotify_timeout;
  uint32_t inotify_delay;

  struct dir_settings dir_settings;  // default dir_settings
  Hashtab *dir_settings_map;         // path -> dir_settings

  struct colors {
    Hashtab *color_map;  // char* -> uint64

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

static inline void config_ratios_set(cvector_vector_type(uint32_t) ratios)
{
  if (cvector_size(ratios) == 0) {
    return;
  }
  cvector_free(cfg.ratios);
  cfg.ratios = ratios;
}

static inline void config_color_map_add(const char *ext, uint64_t channel)
{
  ht_set_copy(cfg.colors.color_map, ext, &channel, sizeof channel);
}

static inline void config_dir_setting_add(const char *path, const struct dir_settings *s)
{
  ht_set_copy(cfg.dir_settings_map, path, s, sizeof *s);
}

static inline void config_icon_map_add(const char *ext, const char *icon)
{
  ht_set_copy(cfg.icon_map, ext, icon, strlen(icon) + 1);
}

void config_colors_clear();
