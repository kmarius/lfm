#pragma once

#include "containers.h"
#include "dir.h"

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#include <unistd.h>

#define MAP_SUGGESTION_DELAY 1000
#define MAP_CLEAR_DELAY 10000
#define LOADING_INDICATOR_DELAY 250

// does not own or free the string
#define i_type vec_str
#define i_val char *
#include "stc/vec.h"

#define i_type hmap_channel
#define i_key char *
#define i_val uint64_t
#define i_keyraw const char *
#define i_keyfrom(p) strdup(p)
#define i_keyto(p) (*p)
#define i_keydrop(p) free(*(p))
#define i_no_clone
#define i_eq(p, q) (!strcmp(*(p), *(q)))
#define i_hash ccharptr_hash
#include "stc/hmap.h"

#define i_type hmap_icon
#define i_key char *
#define i_keyraw const char *
#define i_keyfrom(p) strdup((p))
#define i_keyto(p) (*p)
#define i_keydrop(p) free(*(p))
#define i_val char *
#define i_valraw const char *
#define i_valfrom(p) strdup((p))
#define i_valto(p) (*p)
#define i_valdrop(p) free(*(p))
#define i_eq(p, q) (!strcmp(*(p), *(q)))
#define i_hash ccharptr_hash
#define i_no_clone
#include "stc/hmap.h"

#define i_type hmap_dirsetting
#define i_key char *
#define i_keyraw const char *
#define i_keyfrom(p) strdup((p))
#define i_keyto(p) (*p)
#define i_keydrop(p) free(*(p))
#define i_val struct dir_settings
/* #define i_valraw const char * */
/* #define i_valfrom(p) strdup((p)) */
/* #define i_valto(p) (*p) */
/* #define i_valdrop(p) free(*(p)) */
#define i_eq(p, q) (!strcmp(*(p), *(q)))
#define i_hash ccharptr_hash
#define i_no_clone
#include "stc/hmap.h"

typedef struct config {
  char *configdir;       // ~/.config/lfm
  char *configpath;      // ~/.config/lfm/init.lua
  char *user_configpath; // NULL unless set via commandline
  char *statedir;        // ~/.local/state/lfm
  char *historypath;     // ~/.local/state/lfm/history
  char *datadir;         // /usr/share/lfm
  char *luadir;          // /usr/share/lfm/lua
  char *corepath;        // /usr/share/lfm/lua/core.lua
  char *rundir;          // $XDG_RUNTIME_DIR or /tmp/runtime-$USER
  char *cachedir;        // $XDG_CACHE_HOME/lfm or ~/.cache/lfm
  char *fifopath;        // rundir/$PID.fifo
  char *logpath;         // /tmp/lfm.$PID.log

  int histsize;         // 100
  wchar_t truncatechar; // '~'
  char linkchars[16];   // "->"
  int linkchars_len;
  char *lastdir;
  char *selfile;
  char *startpath;
  char *startfile;
  bool preview;
  bool preview_images;
  char *previewer;
  bool icons;
  hmap_icon icon_map;
  uint32_t scrolloff;
  char *timefmt;
  vec_str commands;
  vec_int ratios;

  vec_str_o inotify_blacklist;
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
