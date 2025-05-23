#include "config.h"

#include "containers.h"
#include "ncutil.h"
#include "notify.h"
#include "stc/cstr.h"

#include <ncurses.h> // COLOR_ constants
#include <notcurses/notcurses.h>

#include <stdlib.h>

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
    cstr_printf(&cfg.rundir, "/tmp/runtime-%s/lfm", getenv("USER"));
  } else {
    cstr_printf(&cfg.rundir, "%s/lfm", xdg_runtime);
  }

  const char *xdg_cache = getenv("XDG_CACHE_HOME");
  if (!xdg_cache || *xdg_cache == 0) {
    cstr_printf(&cfg.cachedir, "%s/.cache/lfm", getenv("HOME"));
  } else {
    cstr_printf(&cfg.cachedir, "%s/lfm", xdg_cache);
  }

  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  if (!xdg_config || *xdg_config == 0) {
    cstr_printf(&cfg.configdir, "%s/.config/lfm", getenv("HOME"));
  } else {
    cstr_printf(&cfg.configdir, "%s/lfm", xdg_config);
  }

  const char *xdg_state = getenv("XDG_STATE_HOME");
  if (!xdg_state || *xdg_state == 0) {
    cstr_printf(&cfg.statedir, "%s/.local/state/lfm", getenv("HOME"));
  } else {
    cstr_printf(&cfg.statedir, "%s/lfm", xdg_state);
  }

  cfg.datadir = cstr_from(default_data_dir);

  cstr_printf(&cfg.configpath, "%s/init.lua", cstr_str(&cfg.configdir));

  cstr_printf(&cfg.historypath, "%s/history", cstr_str(&cfg.statedir));

  cfg.luadir = cstr_from(default_lua_dir);

  cstr_printf(&cfg.corepath, "%s/lfm/core.lua", cstr_str(&cfg.luadir));

  cfg.timefmt = cstr_lit("%Y-%m-%d %H:%M");

#ifdef DEBUG
  cfg.logpath = cstr_lit("/tmp/lfm.debug.log");
  cstr_printf(&cfg.fifopath, "%s/debug.fifo", cstr_str(&cfg.rundir));
#else
  cstr_printf(&cfg.fifopath, "%s/%d.fifo", cstr_str(&cfg.rundir), getpid());
  cstr_printf(&cfg.logpath, "/tmp/lfm.%d.log", getpid());
#endif

  cstr_printf(&cfg.previewer, "%s/runtime/preview.sh", default_data_dir);
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
  cstr_drop(&cfg.configdir);
  cstr_drop(&cfg.configpath);
  cstr_drop(&cfg.corepath);
  cstr_drop(&cfg.statedir);
  cstr_drop(&cfg.datadir);
  cstr_drop(&cfg.cachedir);
  cstr_drop(&cfg.fifopath);
  cstr_drop(&cfg.historypath);
  cstr_drop(&cfg.logpath);
  cstr_drop(&cfg.previewer);
  cstr_drop(&cfg.luadir);
  cstr_drop(&cfg.timefmt);
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
