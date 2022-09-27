#include <linux/limits.h>
#include <ncurses.h>  // COLOR_ constants
#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "cvector.h"
#include "hashtab.h"
#include "log.h"
#include "ncutil.h"
#include "notify.h"
#include "util.h"

// automatically generated, see config/pathdefs.c.in
extern char *default_data_dir;
extern char *default_lua_dir;

Config cfg = {
  .truncatechar = L'~',
  .scrolloff = 4,
  .inotify_timeout = NOTIFY_TIMEOUT,
  .inotify_delay = NOTIFY_DELAY,
  .colors = {
    .normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1),
    .copy = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
    .current = NCCHANNEL_INITIALIZER_PALINDEX(237),
    .delete = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_RED),
    .dir = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLUE, -1),
    .broken = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_RED, -1),
    .exec = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_GREEN, -1),
    .search = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
    .selection = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_MAGENTA),
  }
};


void config_init()
{
  cfg.colors.ext = ht_create(free);
  cvector_vector_type(uint32_t) r = NULL;
  cvector_push_back(r, 1);
  cvector_push_back(r, 2);
  cvector_push_back(r, 3);
  config_ratios_set(r);

  cfg.icon_map  = ht_create(free);
  config_icon_map_add("ln", "l");
  config_icon_map_add("or", "l");
  config_icon_map_add("tw", "t");
  config_icon_map_add("ow", "d");
  config_icon_map_add("st", "t");
  config_icon_map_add("di", "d");
  config_icon_map_add("pi", "p");
  config_icon_map_add("so", "s");
  config_icon_map_add("bd", "b");
  config_icon_map_add("cd", "c");
  config_icon_map_add("su", "u");
  config_icon_map_add("sg", "g");
  config_icon_map_add("ex", "x");
  config_icon_map_add("fi", "-");

  cfg.previewer = strdup("stat");
  cfg.preview = true;

  const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (!xdg_runtime || *xdg_runtime == 0) {
    asprintf(&cfg.rundir, "/tmp/runtime-%s/lfm", getenv("USER"));
  } else {
    asprintf(&cfg.rundir, "%s/lfm", xdg_runtime);
  }

  const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
  if (!xdg_cache_home || *xdg_cache_home == 0) {
    asprintf(&cfg.cachedir, "/home/%s/.cache/lfm", getenv("USER"));
  } else {
    asprintf(&cfg.cachedir, "%s/lfm", xdg_cache_home);
  }

  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  if (!xdg_config || *xdg_config == 0) {
    asprintf(&cfg.configdir, "%s/.config/lfm", getenv("HOME"));
  } else {
    asprintf(&cfg.configdir, "%s/lfm", xdg_config);
  }

  // apparently, there is now XDG_STATE_HOME for history etc.
  const char *xdg_data = getenv("XDG_DATA_HOME");
  if (!xdg_data || *xdg_data == 0) {
    asprintf(&cfg.user_datadir, "%s/.local/share/lfm", getenv("HOME"));
  } else {
    asprintf(&cfg.user_datadir, "%s/lfm", xdg_data);
  }

  cfg.datadir = strdup(default_data_dir);

  asprintf(&cfg.configpath, "%s/init.lua", cfg.configdir);

  asprintf(&cfg.historypath, "%s/history", cfg.user_datadir);

  cfg.luadir = strdup(default_lua_dir);

  asprintf(&cfg.corepath, "%s/lfm.lua", cfg.luadir);

#ifdef DEBUG
  cfg.logpath = strdup("/tmp/lfm.debug.log");
  asprintf(&cfg.fifopath, "%s/debug.fifo", cfg.rundir);
#else
  asprintf(&cfg.fifopath, "%s/%d.fifo", cfg.rundir, getpid());
  asprintf(&cfg.logpath, "/tmp/lfm.%d.log", getpid());
#endif
}


void config_deinit()
{
  config_colors_clear();
  ht_destroy(cfg.colors.ext);
  ht_destroy(cfg.icon_map);
  cvector_free(cfg.ratios);
  cvector_free(cfg.commands);
  cvector_ffree(cfg.inotify_blacklist, free);
  free(cfg.configdir);
  free(cfg.configpath);
  free(cfg.corepath);
  free(cfg.user_datadir);
  free(cfg.datadir);
  free(cfg.cachedir);
  free(cfg.fifopath);
  free(cfg.historypath);
  free(cfg.logpath);
  free(cfg.previewer);
  free(cfg.startfile);
  free(cfg.startpath);
  free(cfg.luadir);
}


void config_colors_clear()
{
  cfg.colors.normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.copy = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.current = NCCHANNEL_INITIALIZER_PALINDEX(237);
  cfg.colors.delete = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.dir = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.broken = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.exec = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.search = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.selection = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);

  ht_clear(cfg.colors.ext);
}
