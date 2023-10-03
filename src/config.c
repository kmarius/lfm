#include <linux/limits.h>
#include <ncurses.h> // COLOR_ constants
#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "hashtab.h"
#include "log.h"
#include "ncutil.h"
#include "notify.h"
#include "util.h"

// automatically generated, see config/pathdefs.c.in
extern char *default_data_dir;
extern char *default_lua_dir;

const char *fileinfo_str[] = {"size", "ctime"};

Config cfg = {
    .truncatechar = L'~',
    .scrolloff = 4,
    .linkchars = "->",
    .linkchars_len = 2,
    .inotify_timeout = NOTIFY_TIMEOUT,
    .inotify_delay = NOTIFY_DELAY,
    .map_suggestion_delay = MAP_SUGGESTION_DELAY,
    .map_clear_delay = MAP_CLEAR_DELAY,
    .loading_indicator_delay = LOADING_INDICATOR_DELAY,
    .colors = {
        .normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1),
        .copy = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
        .current = NCCHANNEL_INITIALIZER_PALINDEX(237),
        .delete = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_RED),
        .dir = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLUE, -1),
        .broken = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_RED, -1),
        .exec = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_GREEN, -1),
        .search = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
        .selection =
            NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_MAGENTA),
    }};

void config_init(void) {
  cfg.colors.color_map = ht_create(xfree);
  cvector_vector_type(uint32_t) r = NULL;
  cvector_push_back(r, 1);
  cvector_push_back(r, 2);
  cvector_push_back(r, 3);
  config_ratios_set(r);

  cfg.icon_map = ht_create(xfree);
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

  cfg.dir_settings_map = ht_create(xfree);
  cfg.dir_settings.dirfirst = true;
  cfg.dir_settings.reverse = false;
  cfg.dir_settings.sorttype = SORT_NATURAL;
  cfg.dir_settings.hidden = false;

  cfg.previewer = strdup("stat");
  cfg.preview = true;

  cfg.histsize = 100;

  const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (!xdg_runtime || *xdg_runtime == 0) {
    asprintf(&cfg.rundir, "/tmp/runtime-%s/lfm", getenv("USER"));
  } else {
    asprintf(&cfg.rundir, "%s/lfm", xdg_runtime);
  }

  const char *xdg_cache = getenv("XDG_CACHE_HOME");
  if (!xdg_cache || *xdg_cache == 0) {
    asprintf(&cfg.cachedir, "/home/%s/.cache/lfm", getenv("USER"));
  } else {
    asprintf(&cfg.cachedir, "%s/lfm", xdg_cache);
  }

  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  if (!xdg_config || *xdg_config == 0) {
    asprintf(&cfg.configdir, "%s/.config/lfm", getenv("HOME"));
  } else {
    asprintf(&cfg.configdir, "%s/lfm", xdg_config);
  }

  const char *xdg_state = getenv("XDG_STATE_HOME");
  if (!xdg_state || *xdg_state == 0) {
    asprintf(&cfg.statedir, "%s/.local/state/lfm", getenv("HOME"));
  } else {
    asprintf(&cfg.statedir, "%s/lfm", xdg_state);
  }

  cfg.datadir = strdup(default_data_dir);

  asprintf(&cfg.configpath, "%s/init.lua", cfg.configdir);

  asprintf(&cfg.historypath, "%s/history", cfg.statedir);

  cfg.luadir = strdup(default_lua_dir);

  asprintf(&cfg.corepath, "%s/lfm/core.lua", cfg.luadir);

  cfg.timefmt = strdup("%Y-%m-%d %H:%M");

#ifdef DEBUG
  cfg.logpath = strdup("/tmp/lfm.debug.log");
  asprintf(&cfg.fifopath, "%s/debug.fifo", cfg.rundir);
#else
  asprintf(&cfg.fifopath, "%s/%d.fifo", cfg.rundir, getpid());
  asprintf(&cfg.logpath, "/tmp/lfm.%d.log", getpid());
#endif
}

void config_deinit(void) {
  config_colors_clear();
  ht_destroy(cfg.colors.color_map);
  ht_destroy(cfg.icon_map);
  ht_destroy(cfg.dir_settings_map);
  cvector_free(cfg.ratios);
  cvector_free(cfg.commands);
  cvector_ffree(cfg.inotify_blacklist, xfree);
  xfree(cfg.configdir);
  xfree(cfg.configpath);
  xfree(cfg.user_configpath);
  xfree(cfg.corepath);
  xfree(cfg.statedir);
  xfree(cfg.datadir);
  xfree(cfg.cachedir);
  xfree(cfg.fifopath);
  xfree(cfg.historypath);
  xfree(cfg.logpath);
  xfree(cfg.previewer);
  xfree(cfg.startfile);
  xfree(cfg.startpath);
  xfree(cfg.luadir);
  xfree(cfg.timefmt);
}

void config_colors_clear(void) {
  cfg.colors.normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.copy = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.current = NCCHANNEL_INITIALIZER_PALINDEX(237);
  cfg.colors.delete = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.dir = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.broken = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.exec = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.search = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);
  cfg.colors.selection = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1);

  ht_clear(cfg.colors.color_map);
}
