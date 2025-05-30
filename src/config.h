#pragma once

#include "bytes.h"
#include "dir.h"
#include "keys.h"
#include "vec_cstr.h"
#include "vec_int.h"

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#include <unistd.h>

#define MAP_SUGGESTION_DELAY 1000
#define MAP_CLEAR_DELAY 10000
#define LOADING_INDICATOR_DELAY 250

// maps file extensions to fg/bg channel
#define i_type hmap_channel
#define i_keypro cstr
#define i_val uint64_t
#define i_no_clone
#include "stc/hmap.h"

#define i_type hmap_icon
#define i_keypro cstr
#define i_valpro cstr
#define i_no_clone
#include "stc/hmap.h"

#define i_type hmap_dirsetting
#define i_val struct dir_settings
#define i_key cstr
#define i_keyraw zsview
#define i_keyfrom cstr_from_zv
#define i_keytoraw cstr_zv
#define i_keydrop cstr_drop
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_no_clone
#include "stc/hmap.h"

typedef struct config {
  cstr configdir;   // ~/.config/lfm
  cstr configpath;  // ~/.config/lfm/init.lua
  cstr statedir;    // ~/.local/state/lfm
  cstr historypath; // ~/.local/state/lfm/history
  cstr datadir;     // /usr/share/lfm
  cstr luadir;      // /usr/share/lfm/lua
  cstr corepath;    // /usr/share/lfm/lua/core.lua
  cstr rundir;      // $XDG_RUNTIME_DIR or /tmp/runtime-$USER
  cstr cachedir;    // $XDG_CACHE_HOME/lfm or ~/.cache/lfm
  cstr fifopath;    // rundir/$PID.fifo
  cstr logpath;     // /tmp/lfm.$PID.log

  int histsize;         // 100
  wchar_t truncatechar; // '~'
  char linkchars[16];   // "->"
  char current_char;    // \0, unless 8 color terminal
  int linkchars_len;
  cstr infoline;
  bool preview;
  bool preview_images;
  bytes lua_previewer;
  cstr previewer;
  uint32_t preview_delay;
  bool icons;
  bool tags;
  hmap_icon icon_map;
  uint32_t scrolloff;
  cstr timefmt;
  vec_int ratios;
  input_t mapleader;

  vec_cstr inotify_blacklist;
  uint32_t inotify_timeout;
  uint32_t inotify_delay;

  uint32_t map_suggestion_delay;
  uint32_t map_clear_delay;
  uint32_t loading_indicator_delay;

  struct dir_settings dir_settings; // default dir_settings
  hmap_dirsetting dir_settings_map; // path -> dir_settings

  struct colors {
    hmap_channel color_map; // char* -> uint64

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

void config_init(void);

void config_deinit(void);

void config_colors_clear(void);
