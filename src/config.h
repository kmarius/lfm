#pragma once

#include "dir.h"
#include "keys.h"
#include "types/bytes.h"
#include "types/vec_cstr.h"
#include "types/vec_env.h"
#include "types/vec_int.h"

#include <stdbool.h>

#define MAP_SUGGESTION_DELAY 1000
#define MAP_CLEAR_DELAY 10000
#define LOADING_INDICATOR_DELAY 250

// maps file extensions to fg/bg channel
#define i_type hmap_channel
#define i_keypro cstr
#define i_val u64
#define i_no_clone
#include <stc/hmap.h>

#define i_type hmap_icon
#define i_keypro cstr
#define i_valpro cstr
#define i_no_clone
#include <stc/hmap.h>

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
#include <stc/hmap.h>

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

  u32 histsize;          // 100
  char truncatechar[16]; // '~'
  char linkchars[16];    // "->"
  char current_char;     // \0, unless 8 color terminal
  i32 linkchars_len;
  cstr infoline;
  bool preview;
  bool preview_images;
  bytes lua_previewer;
  cstr previewer;
  u32 preview_delay;
  bool icons;
  bool tags;
  hmap_icon icon_map;
  u32 scrolloff;
  cstr timefmt;
  vec_int ratios;
  input_t mapleader;

  vec_cstr inotify_blacklist;
  u32 inotify_timeout;
  u32 inotify_delay;

  u32 map_suggestion_delay;
  u32 map_clear_delay;
  u32 loading_indicator_delay;

  struct dir_settings dir_settings; // default dir_settings
  hmap_dirsetting dir_settings_map; // path -> dir_settings

  vec_env extra_env; // environment overrides for forked processes

  struct colors {
    hmap_channel color_map; // char* -> uint64

    u64 normal;
    u64 selection;
    u64 copy;
    u64 delete;
    u64 search;
    u64 broken;
    u64 exec;
    u64 dir;
    u32 current; /* bg channel index only */
  } colors;
} Config;

extern Config cfg;

void config_init(void);

void config_deinit(void);

void config_colors_clear(void);
