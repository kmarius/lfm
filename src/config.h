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
  char *user_datadir;   // ~/.local/share/lfm
  char *historypath;    // ~/.local/share/lfm/history
  char *datadir;        // /usr/share/lfm
  char *luadir;         // /usr/share/lfm/lua
  char *corepath;       // /usr/share/lfm/lua/core.lua
  char *rundir;         // $XDG_RUNTIME_DIR or /tmp/runtime-$USER
  char *cachedir;       // $XDG_CACHE_HOME/lfm or ~/.cache/lfm
  char *fifopath;       // rundir/$PID.fifo
  char *logpath;        // /tmp/lfm.$PID.log

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

static inline void config_ratios_set(cvector_vector_type(uint32_t) ratios)
{
  if (cvector_size(ratios) == 0) {
    return;
  }
  cvector_free(cfg.ratios);
  cfg.ratios = ratios;
}

static inline void config_ext_channel_add(const char *ext, uint64_t channel)
{
  char *s = malloc(sizeof(uint64_t) + strlen(ext) + 1);
  *(uint64_t *) s = channel;
  strcpy(s + sizeof(uint64_t), ext);
  ht_set(cfg.colors.ext, s + sizeof(uint64_t), s);
}

static inline void config_dir_setting_add(const char *path, const struct dir_settings *s)
{
  char *val = malloc((sizeof *s) + strlen(path) + 1);
  memcpy(val, s, sizeof *s);
  strcpy(val + sizeof *s, path);
  ht_set(cfg.dir_settings_map, val + sizeof *s, val);
}

void config_colors_clear();

static inline void config_icon_map_add(const char *ext, const char *icon)
{
  const size_t l = strlen(icon);
  char *val = malloc(l + strlen(ext) + 2);
  strcpy(val, icon);
  strcpy(val + l + 1, ext);
  ht_set(cfg.icon_map, val + l + 1, val);
}
