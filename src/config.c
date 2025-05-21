#include "config.h"

#include "containers.h"
#include "memory.h"
#include "ncutil.h"
#include "notify.h"
#include "stc/cstr.h"
#include "util.h"

#include <ncurses.h> // COLOR_ constants
#include <notcurses/notcurses.h>

#include <stdlib.h>
#include <string.h>

#include <linux/limits.h>

// automatically generated, see config/pathdefs.c.in
extern char *default_data_dir;
extern char *default_lua_dir;

// fields not listed deliberately initialized to 0
Config cfg = {
    .histsize = 100,
    .truncatechar = L'~',
    .scrolloff = 4,
    .linkchars = "->",
    .linkchars_len = 2,
    .inotify_timeout = NOTIFY_TIMEOUT,
    .inotify_delay = NOTIFY_DELAY,
    .map_suggestion_delay = MAP_SUGGESTION_DELAY,
    .map_clear_delay = MAP_CLEAR_DELAY,
    .loading_indicator_delay = LOADING_INDICATOR_DELAY,
    .colors =
        {
            .normal = NCCHANNELS_INITIALIZER_PALINDEX(-1, -1),
            .copy = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
            .current =
                NCCHANNEL_INITIALIZER_PALINDEX(237), // doesn't exist in tty
            .delete = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_RED),
            .dir = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLUE, -1),
            .broken = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_RED, -1),
            .exec = NCCHANNELS_INITIALIZER_PALINDEX(COLOR_GREEN, -1),
            .search =
                NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_YELLOW),
            .selection =
                NCCHANNELS_INITIALIZER_PALINDEX(COLOR_BLACK, COLOR_MAGENTA),
        },
    .dir_settings =
        {
            .dirfirst = true,
            .reverse = false,
            .sorttype = SORT_NATURAL,
            .hidden = false,
        },
};

void config_init(void) {
  const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (!xdg_runtime || *xdg_runtime == 0) {
    asprintf(&cfg.rundir, "/tmp/runtime-%s/lfm", getenv("USER"));
  } else {
    asprintf(&cfg.rundir, "%s/lfm", xdg_runtime);
  }

  const char *xdg_cache = getenv("XDG_CACHE_HOME");
  if (!xdg_cache || *xdg_cache == 0) {
    asprintf(&cfg.cachedir, "%s/.cache/lfm", getenv("HOME"));
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

  cstr_printf(&cfg.previewer,"%s/runtime/preview.sh", default_data_dir);
  cfg.preview = true;

  vec_int_reserve(&cfg.ratios, 3);
  vec_int_push(&cfg.ratios, 1);
  vec_int_push(&cfg.ratios, 2);
  vec_int_push(&cfg.ratios, 3);

  hmap_icon_emplace_or_assign(&cfg.icon_map, "ln", "l");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "or", "l");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "tw", "t");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "ow", "d");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "st", "t");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "di", "d");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "pi", "p");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "so", "s");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "bd", "b");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "cd", "c");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "su", "u");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "sg", "g");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "ex", "x");
  hmap_icon_emplace_or_assign(&cfg.icon_map, "fi", "-");
}

void config_deinit(void) {
  config_colors_clear();
  hmap_channel_drop(&cfg.colors.color_map);
  hmap_icon_drop(&cfg.icon_map);
  hmap_dirsetting_drop(&cfg.dir_settings_map);
  vec_int_drop(&cfg.ratios);
  vec_cstr_drop(&cfg.inotify_blacklist);
  xfree(cfg.configdir);
  xfree(cfg.configpath);
  xfree(cfg.corepath);
  xfree(cfg.statedir);
  xfree(cfg.datadir);
  xfree(cfg.cachedir);
  xfree(cfg.fifopath);
  xfree(cfg.historypath);
  xfree(cfg.logpath);
  cstr_drop(&cfg.previewer);
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

  hmap_channel_clear(&cfg.colors.color_map);
}
