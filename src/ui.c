#include "ui.h"

#include "async.h"
#include "cmdline.h"
#include "config.h"
#include "dir.h"
#include "file.h"
#include "filter.h"
#include "fm.h"
#include "hooks.h"
#include "infoline.h"
#include "input.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros_defs.h"
#include "memory.h"
#include "mode.h"
#include "ncutil.h"
#include "preview.h"
#include "spinner.h"
#include "statusline.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "util.h"

#include <ev.h>
#include <ncurses.h>
#include <notcurses/notcurses.h>

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>

#include "stc/common.h"

#define i_declared
#define i_type vec_ncplane, struct ncplane *
#define i_keydrop(p) (ncplane_destroy(*(p)))
#define i_no_clone
#include "stc/vec.h"

#define EXT_MAX_LEN 128 // to convert the extension to lowercase

static void menu_delay_timer_cb(EV_P_ ev_timer *w, int revents);
static void draw_dirs(Ui *ui);
static void plane_draw_dir(struct ncplane *n, Dir *dir, pathlist *sel,
                           pathlist *load, paste_mode mode, zsview highlight,
                           bool print_info);
static void draw_preview(Ui *ui);
static void draw_menu(Ui *ui, const vec_cstr *menu);
static void menu_resize(Ui *ui);
static inline void print_message(Ui *ui, zsview msg, bool error);
static inline void draw_cmdline(Ui *ui);
static void redraw_cb(EV_P_ ev_idle *w, int revents);
static int resize_cb(struct ncplane *n);

static void on_cursor_resting(EV_P_ ev_timer *w, int revents);

/* init/resize {{{ */

void ui_init(Ui *ui) {
  ev_idle_init(&ui->redraw_watcher, redraw_cb);
  ui->redraw_watcher.data = ui;
  ev_idle_start(to_lfm(ui)->loop, &ui->redraw_watcher);

  ev_timer_init(&ui->preview_load_timer, on_cursor_resting, 0,
                cfg.preview_delay / 1000.0);
  ui->preview_load_timer.data = to_lfm(ui);

  cmdline_init(&ui->cmdline);
  infoline_init(ui);
  input_init(to_lfm(ui));
  ui_resume(ui);

  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ui->winsize);
  log_trace("winsize rows=%u cols=%u ypixel=%u, xpixel=u", ui->winsize.ws_col,
            ui->winsize.ws_row, ui->winsize.ws_ypixel, ui->winsize.ws_xpixel);
}

void ui_deinit(Ui *ui) {
  ui_suspend(ui);
  vec_message_drop(&ui->messages);
  vec_cstr_drop(&ui->menubuf);
  vec_ncplane_drop(&ui->planes.dirs);
  cmdline_deinit(&ui->cmdline);
  cstr_drop(&ui->search_string);
  cstr_drop(&ui->infoline);
}

static int resize_cb(struct ncplane *n) {
  Ui *ui = ncplane_userptr(n);
  notcurses_stddim_yx(ui->nc, &ui->y, &ui->x);
  ncplane_resize(ui->planes.info, 0, 0, 0, 0, 0, 0, 1, ui->x);
  ncplane_resize(ui->planes.cmdline, 0, 0, 0, 0, 0, 0, 1, ui->x);
  ncplane_move_yx(ui->planes.cmdline, ui->y - 1, 0);
  lfm_run_hook(to_lfm(ui), LFM_HOOK_RESIZED);
  ui_recol(ui);
  Fm *fm = &to_lfm(ui)->fm;
  fm_on_resize(fm, ui->y - 2);
  menu_resize(ui);
  ui_update_file_preview(ui);
  return 0;
}

void ui_resume(Ui *ui) {
  log_debug("resuming ui");
  kbblocking(false);
  struct notcurses_options ncopts = {
      .flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS |
               NCOPTION_PRESERVE_CURSOR | NCOPTION_NO_QUIT_SIGHANDLERS,
  };
  log_debug("creating notcurses context");
  ui->nc = notcurses_init(&ncopts, NULL);
  if (!ui->nc) {
    exit(EXIT_FAILURE);
  }

  if (notcurses_palette_size(ui->nc) == 8) {
    if (cfg.current_char == 0) {
      cfg.current_char = '>';
    }
  }

  struct ncplane *ncstd = notcurses_stdplane(ui->nc);

  ncplane_dim_yx(ncstd, &ui->y, &ui->x);
  to_lfm(ui)->fm.height = ui->y - 2;

  struct ncplane_options opts = {
      .y = 0,
      .x = 0,
      .rows = 1,
      .cols = ui->x,
      .userptr = ui,
  };

  // this plane is responsible for resizing, only put the callback here
  opts.resizecb = resize_cb;
  ui->planes.info = ncplane_create(ncstd, &opts);
  opts.resizecb = NULL;

  spinner_init(&ui->spinner, spinner_chars, to_lfm(ui)->loop, ui->planes.info);

  opts.y = ui->y - 1;
  ui->planes.cmdline = ncplane_create(ncstd, &opts);

  ui_recol(ui);

  opts.rows = opts.cols = 1;
  ui->planes.menu = ncplane_create(ncstd, &opts);
  ncplane_move_bottom(ui->planes.menu);

  ev_timer_init(&ui->menu_delay_timer, menu_delay_timer_cb, 0, 0);
  ui->menu_delay_timer.data = to_lfm(ui);

  input_resume(to_lfm(ui));
  ui_update_file_preview(ui);
  ui_redraw(ui, REDRAW_FM);
  ui->running = true;
}

void ui_suspend(Ui *ui) {
  log_debug("suspending ui");
  ui->running = false;
  // this breaks if called after ev_break, for now, ensure that the spinner is
  // re-initialized in ui_resume before the event-loop calls its callback with
  // invalid notcurses spinner_off(&ui->spinner);
  input_suspend(to_lfm(ui));
  vec_ncplane_clear(&ui->planes.dirs);
  ncplane_destroy(ui->planes.cmdline);
  ncplane_destroy(ui->planes.menu);
  ncplane_destroy(ui->planes.info);
  log_debug("destroying notcurses context");
  notcurses_stop(ui->nc);
  ui->nc = NULL;
  ui->planes.cmdline = NULL;
  ui->planes.menu = NULL;
  ui->planes.info = NULL;
  ui->nc = NULL;
  if (ui->preview.preview) {
    ui->preview.preview = NULL;
  }
  kbblocking(true);
}

void kbblocking(bool blocking) {
  int val = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (val != -1) {
    fcntl(STDIN_FILENO, F_SETFL,
          blocking ? val & ~O_NONBLOCK : val | O_NONBLOCK);
  }
}

void ui_recol(Ui *ui) {
  // get the terminal geometry here, this function is called on startup and
  // resize
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ui->winsize);
  ui->ypixel_cell = ui->winsize.ws_ypixel / ui->winsize.ws_row;
  ui->xpixel_cell = ui->winsize.ws_xpixel / ui->winsize.ws_col;

  log_debug(
      "winsize row=%u col=%u ypixel=%u xpixel=%u ypixel_cell=%u xpixel_cell=%u",
      ui->winsize.ws_col, ui->winsize.ws_row, ui->winsize.ws_ypixel,
      ui->winsize.ws_xpixel, ui->ypixel_cell, ui->xpixel_cell);

  struct ncplane *ncstd = notcurses_stdplane(ui->nc);

  vec_ncplane_clear(&ui->planes.dirs);

  ui->num_columns = vec_int_size(&cfg.ratios);

  uint32_t sum = 0;
  c_foreach(it, vec_int, cfg.ratios) {
    sum += *it.ref;
  }

  struct ncplane_options opts = {
      .y = 1,
      .rows = ui->y > 2 ? ui->y - 2 : 1,
  };

  uint32_t xpos = 0;
  for (uint32_t i = 0; i < ui->num_columns - 1; i++) {
    opts.cols =
        (ui->x - ui->num_columns + 1) * *vec_int_at(&cfg.ratios, i) / sum;
    if (opts.cols == 0) {
      opts.cols = 1;
    }
    opts.x = xpos;
    vec_ncplane_push(&ui->planes.dirs, ncplane_create(ncstd, &opts));
    xpos += opts.cols + 1;
  }
  opts.x = xpos;
  opts.cols = ui->x - xpos - 1;
  vec_ncplane_push(&ui->planes.dirs, ncplane_create(ncstd, &opts));
  ui->planes.preview = *vec_ncplane_back(&ui->planes.dirs);
  /* ui->planes.preview = ui->planes.dirs[ui->num_columns - 1]; */
  ui->preview.x = opts.cols;
  ui->preview.y = ui->y - 2;
}

/* }}} */

/* main drawing/echo/err {{{ */

static void redraw_cb(EV_P_ ev_idle *w, int revents) {
  (void)revents;
  ui_draw(w->data);
  ev_idle_stop(EV_A_ w);
}

void ui_draw(Ui *ui) {
  uint64_t t0 = current_micros();

  if (ui->redraw & REDRAW_FM) {
    draw_dirs(ui);
  }
  if (ui->redraw & (REDRAW_MENU | REDRAW_MENU)) {
    draw_menu(ui, &ui->menubuf);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_CMDLINE)) {
    draw_cmdline(ui);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_INFO)) {
    infoline_draw(ui);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_PREVIEW)) {
    draw_preview(ui);
  }
  if (ui->redraw) {
    notcurses_render(ui->nc);
  }

  uint64_t t1 = current_micros();
  if (ui->redraw)
    log_trace("ui_draw completed in %.3fms (%d)", (t1 - t0) / 1000.0,
              ui->redraw);

  ui->redraw = 0;
}

void ui_clear(Ui *ui) {
  notcurses_refresh(ui->nc, NULL, NULL);

  notcurses_cursor_enable(ui->nc, 0, 0);
  notcurses_cursor_disable(ui->nc);

  ui_redraw(ui, REDRAW_FULL);
}

static void draw_dirs(Ui *ui) {
  Fm *fm = &to_lfm(ui)->fm;
  const uint32_t l = fm->dirs.length;
  for (uint32_t i = 0; i < l; i++) {
    plane_draw_dir(*vec_ncplane_at(&ui->planes.dirs, l - i - 1),
                   fm->dirs.visible.data[i], &fm->selection.current,
                   &fm->paste.buffer, fm->paste.mode,
                   i == 0 ? ui->highlight : zsview_init(), i == 0);
  }
}

static void draw_preview(Ui *ui) {
  Fm *fm = &to_lfm(ui)->fm;
  if (cfg.preview && ui->num_columns > 1) {
    if (fm->dirs.preview) {
      plane_draw_dir(ui->planes.preview, fm->dirs.preview,
                     &fm->selection.current, &fm->paste.buffer, fm->paste.mode,
                     zsview_init(), false);
    } else {
      if (ui->preview.preview) {
        preview_draw(ui->preview.preview, ui->planes.preview);
      } else {
        ui_update_file_preview(ui);
        ncplane_erase(ui->planes.preview);
      }
    }
  }
}

void ui_echom(Ui *ui, const char *format, ...) {
  va_list args;
  va_start(args, format);
  ui_vechom(ui, format, args);
  va_end(args);
  ui_redraw(ui, REDRAW_CMDLINE);
}

void ui_error(Ui *ui, const char *format, ...) {
  va_list args;
  va_start(args, format);
  ui_verror(ui, format, args);
  va_end(args);
  ui_redraw(ui, REDRAW_CMDLINE);
}

void ui_verror(Ui *ui, const char *fmt, va_list args) {
  struct message msg = {.error = true};
  cstr_vfmt(&msg.text, 0, fmt, args);

  log_error("%s", cstr_str(&msg.text));

  vec_message_push(&ui->messages, msg);

  ui->show_message = true;
}

void ui_vechom(Ui *ui, const char *fmt, va_list args) {
  struct message msg = {};
  cstr_vfmt(&msg.text, 0, fmt, args);

  vec_message_push(&ui->messages, msg);

  ui->show_message = true;
}

/* }}} */

/* menu {{{ */

/* most notably, replaces tabs with (up to) 8 spaces */
static void draw_menu(Ui *ui, const vec_cstr *menubuf) {
  if (!menubuf || !ui->menu_visible) {
    return;
  }

  struct ncplane *n = ui->planes.menu;

  ncplane_erase(n);

  /* needed to draw over directories */
  ncplane_set_base(n, " ", 0, 0);

  int i = 0;
  c_foreach(it, vec_cstr, *menubuf) {
    ncplane_cursor_move_yx(n, i++, 0);

    const char *str = cstr_str(it.ref);
    const char *end = cstr_str(it.ref) + cstr_size(it.ref);
    uint32_t xpos = 0;

    while (str < end) {
      const char *start = str;
      while (str < end && *str != '\t' && *str != '\033') {
        str++;
      }
      xpos += ncplane_putnstr(n, str - start, start);
      if (*str == '\033') {
        str = ncplane_set_ansi_attrs(n, str);
      } else if (*str == '\t') {
        ncplane_putchar(n, ' ');
        xpos++;
        for (const uint32_t l = ((xpos / 8) + 1) * 8; xpos < l; xpos++) {
          ncplane_putchar(n, ' ');
        }
        str++;
      }
    }
  }
}

static void menu_resize(Ui *ui) {
  int buf_sz = vec_cstr_size(&ui->menubuf);
  const uint32_t h = max(1, min(buf_sz, ui->y - 2));
  ncplane_resize(ui->planes.menu, 0, 0, 0, 0, 0, 0, h, ui->x);
  ncplane_move_yx(ui->planes.menu, ui->y - 1 - h, 0);
  if (buf_sz) {
    ncplane_move_top(ui->planes.menu);
  }
}

void ui_menu_show(Ui *ui, vec_cstr *vec, uint32_t delay) {
  struct ev_loop *loop = to_lfm(ui)->loop;
  ev_timer_stop(EV_A_ & ui->menu_delay_timer);
  if (vec_cstr_size(&ui->menubuf) > 0) {
    ncplane_erase(ui->planes.menu);
    ncplane_move_bottom(ui->planes.menu);
    vec_cstr_clear(&ui->menubuf);
    ui->menu_visible = false;
  }
  if (vec) {
    vec_cstr_take(&ui->menubuf, *vec);

    if (delay > 0) {
      ui->menu_delay_timer.repeat = (float)delay / 1000.0;
      ev_timer_again(EV_A_ & ui->menu_delay_timer);
    } else {
      ev_invoke(EV_A_ & ui->menu_delay_timer, 0);
    }
  }
  if (vec_cstr_size(&ui->menubuf) > 0) {
    ui_redraw(ui, REDRAW_MENU);
  }
}

static void menu_delay_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  Ui *ui = &lfm->ui;
  if (vec_cstr_size(&ui->menubuf) > 0) {
    menu_resize(ui);
    ncplane_move_top(ui->planes.menu);
    ui->menu_visible = true;
  }
  ui_redraw(ui, REDRAW_MENU);
  ev_timer_stop(EV_A_ w);
  ev_idle_start(EV_A_ & ui->redraw_watcher);
}

/* }}} */

/* draw_dir {{{ */

static uint64_t ext_channel_get(const char *ext) {
  char buf[EXT_MAX_LEN];

  if (ext) {
    // lowercase for ascii - good enough for now
    size_t i;
    for (i = 0; ext[i] && i < EXT_MAX_LEN - 1; i++) {
      buf[i] = tolower(ext[i]);
    }
    buf[i] = 0;
    hmap_channel_iter it = hmap_channel_find(&cfg.colors.color_map, buf);
    if (it.ref) {
      return it.ref->second;
    }
  }
  return 0;
}

static inline int print_shortened(struct ncplane *n, const char *name,
                                  int max_len, bool has_ext) {
  if (max_len <= 0) {
    return 0;
  }

  int name_len;
  wchar_t *namew = ambstowcs(name, &name_len);
  int ret = print_shortened_w(n, namew, name_len, max_len, has_ext);
  xfree(namew);
  return ret;
}

static int print_highlighted_and_shortened(struct ncplane *n, const char *name,
                                           const char *hl, int max_len,
                                           bool has_ext) {
  if (max_len <= 0) {
    return 0;
  }

  int name_len, hl_len;
  wchar_t *namew_ = ambstowcs(name, &name_len);
  wchar_t *hlw = ambstowcs(hl, &hl_len);
  wchar_t *extw = has_ext ? wcsrchr(namew_, L'.') : NULL;
  if (!extw || extw == namew_) {
    extw = namew_ + name_len;
  }
  int ext_len = name_len - (extw - namew_);
  const wchar_t *hl_begin = wstrcasestr(namew_, hlw);
  const wchar_t *hl_end = hl_begin + hl_len;

  const uint64_t ch = ncplane_channels(n);
  int x = max_len;
  wchar_t *namew = namew_;

  /* TODO: some of these branches can probably be optimized/combined (on
   * 2022-02-18) */
  if (name_len <= max_len) {
    // everything fits
    while (namew < hl_begin) {
      ncplane_putwc(n, *(namew++));
    }
    ncplane_set_channels(n, cfg.colors.search);
    while (namew < hl_end) {
      ncplane_putwc(n, *(namew++));
    }
    ncplane_set_channels(n, ch);
    while (*namew) {
      ncplane_putwc(n, *(namew++));
    }
    x = name_len;
  } else if (max_len > ext_len + 1) {
    // print extension and as much of the name as possible
    wchar_t *print_name_end = namew + max_len - ext_len - 1;
    if (hl_begin < print_name_end) {
      // highlight begins before truncate
      while (namew < hl_begin) {
        ncplane_putwc(n, *(namew++));
      }
      ncplane_set_channels(n, cfg.colors.search);
      if (hl_end <= print_name_end) {
        // highlight ends before truncate
        while (namew < hl_end) {
          ncplane_putwc(n, *(namew++));
        }
        ncplane_set_channels(n, ch);
        while (namew < print_name_end) {
          ncplane_putwc(n, *(namew++));
        }
      } else {
        // highlight continues during truncate
        while (namew < print_name_end) {
          ncplane_putwc(n, *(namew++));
        }
      }
      ncplane_putwc(n, cfg.truncatechar);
    } else {
      // highlight begins after truncate
      while (namew < print_name_end) {
        ncplane_putwc(n, *(namew++));
      }
      if (hl_begin < extw) {
        // highlight begins before extension begins
        ncplane_set_channels(n, cfg.colors.search);
      }
      ncplane_putwc(n, cfg.truncatechar);
    }
    if (hl_begin >= extw) {
      while (extw < hl_begin) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_set_channels(n, cfg.colors.search);
      while (extw < hl_end) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_set_channels(n, ch);
      while (*extw) {
        ncplane_putwc(n, *(extw++));
      }
    } else {
      // highlight was started before
      while (extw < hl_end) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_set_channels(n, ch);
      while (*extw) {
        ncplane_putwc(n, *(extw++));
      }
    }
  } else if (max_len >= 5) {
    const wchar_t *ext_end = extw + max_len - 2 - 1;
    if (hl_begin == namew_) {
      ncplane_set_channels(n, cfg.colors.search);
    }
    ncplane_putwc(n, *name);
    if (hl_begin < extw) {
      ncplane_set_channels(n, cfg.colors.search);
    }
    ncplane_putwc(n, cfg.truncatechar);
    if (hl_end <= extw) {
      ncplane_set_channels(n, ch);
    }
    if (hl_begin >= extw) {
      while (extw < hl_begin) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_set_channels(n, cfg.colors.search);
      if (hl_end < ext_end) {
        while (extw < hl_end) {
          ncplane_putwc(n, *(extw++));
        }
        ncplane_set_channels(n, ch);
      }
      while (extw < ext_end) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_putwc(n, cfg.truncatechar);
      ncplane_set_channels(n, ch);
    } else {
      while (extw < ext_end) {
        ncplane_putwc(n, *(extw++));
      }
      ncplane_putwc(n, cfg.truncatechar);
    }
  } else if (max_len > 1) {
    const wchar_t *name_end = namew_ + max_len - 1;
    if (hl_begin < name_end) {
      while (namew < hl_begin) {
        ncplane_putwc(n, *(namew++));
      }
      ncplane_set_channels(n, cfg.colors.search);
      if (hl_end < name_end) {
        while (namew < hl_end) {
          ncplane_putwc(n, *(namew++));
        }
        ncplane_set_channels(n, ch);
        while (namew < name_end) {
          ncplane_putwc(n, *(namew++));
        }
      } else {
        while (namew < name_end) {
          ncplane_putwc(n, *(namew++));
        }
      }
    } else {
      while (namew < name_end) {
        ncplane_putwc(n, *(namew++));
      }
      ncplane_set_channels(n, cfg.colors.search);
    }
    ncplane_putwc(n, cfg.truncatechar);
    ncplane_set_channels(n, ch);
  } else {
    // only one char
    if (hl == name) {
      const uint64_t ch = ncplane_channels(n);
      ncplane_set_channels(n, cfg.colors.search);
      ncplane_putwc(n, *namew_);
      ncplane_set_channels(n, ch);
    } else {
      ncplane_putwc(n, *namew_);
    }
  }

  xfree(hlw);
  xfree(namew_);
  return x;
}

static void draw_file(struct ncplane *n, const File *file, bool iscurrent,
                      pathlist *sel, pathlist *load, paste_mode mode,
                      zsview highlight, bool print_info, fileinfo fileinfo) {
  unsigned int ncol, y0;
  unsigned int x = 0;
  char info[32];
  ncplane_dim_yx(n, NULL, &ncol);
  ncplane_cursor_yx(n, &y0, NULL);

  int rightmargin = 0;

  if (print_info) {
    switch (fileinfo) {
    case INFO_SIZE:
      if (file_isdir(file)) {
        if (file_dircount(file) < 0) {
          snprintf(info, sizeof info, "?");
        } else {
          snprintf(info, sizeof info, "%d", file_dircount(file));
        }
      } else {
        file_size_readable(file, info);
      }
      break;
    case INFO_ATIME: {
      struct tm *tm = localtime(&file->stat.st_atim.tv_sec);
      strftime(info, sizeof info, cstr_str(&cfg.timefmt), tm);
    } break;
    case INFO_CTIME: {
      struct tm *tm = localtime(&file->stat.st_ctim.tv_sec);
      strftime(info, sizeof info, cstr_str(&cfg.timefmt), tm);
    } break;
    case INFO_MTIME: {
      struct tm *tm = localtime(&file->stat.st_mtim.tv_sec);
      strftime(info, sizeof info, cstr_str(&cfg.timefmt), tm);
    } break;
    case NUM_FILEINFO:
    default: {
    }
    }
    rightmargin = strlen(info) + 1;

    if (file_islink(file) && cfg.linkchars_len > 0) {
      rightmargin += cfg.linkchars_len;
      rightmargin++;
    }
    if (file_ext(file)) {
      if (ncol - 3 - rightmargin - (cfg.icons ? 2 : 0) < 4) {
        rightmargin = 0;
      }
    } else {
      if (ncol - 3 - rightmargin - (cfg.icons ? 2 : 0) < 2) {
        rightmargin = 0;
      }
    }
  }

  ncplane_set_bg_default(n);

  if (pathlist_contains(sel, file_path(file))) {
    ncplane_set_channels(n, cfg.colors.selection);
  } else if (mode == PASTE_MODE_MOVE &&
             pathlist_contains(load, file_path(file))) {
    ncplane_set_channels(n, cfg.colors.delete);
  } else if (mode == PASTE_MODE_COPY &&
             pathlist_contains(load, file_path(file))) {
    ncplane_set_channels(n, cfg.colors.copy);
  }

  // this is needed because when selecting with space the filename is printed
  // as black (bug in notcurses)
  // 2021-08-21
  ncplane_set_fg_default(n);

  ncplane_putchar(n, ' ');
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);

  if (file_isdir(file)) {
    ncplane_set_channels(n, cfg.colors.dir);
    ncplane_set_styles(n, NCSTYLE_BOLD);
  } else if (file_isbroken(file) || file_error(file)) {
    ncplane_set_channels(n, cfg.colors.broken);
  } else if (file_isexec(file)) {
    ncplane_set_channels(n, cfg.colors.exec);
  } else {
    uint64_t ch = ext_channel_get(file_ext(file));
    if (ch > 0) {
      ncplane_set_channels(n, ch);
    } else {
      ncplane_set_channels(n, cfg.colors.normal);
      /* ncplane_set_fg_default(n); */
    }
  }

  if (cfg.current_char) {
    if (iscurrent) {
      uint64_t channels = ncplane_channels(n);
      uint16_t styles = ncplane_styles(n);
      ncplane_set_styles(n, NCSTYLE_NONE);
      ncplane_set_fg_default(n);
      ncplane_set_bg_default(n);
      ncplane_putchar(n, cfg.current_char);
      ncplane_set_styles(n, styles);
      ncplane_set_channels(n, channels);
    } else {
      ncplane_putchar(n, ' ');
    }
  }

  if (iscurrent) {
    ncplane_set_bchannel(n, cfg.colors.current);
  }

  ncplane_putchar(n, ' ');

  if (cfg.icons) {
    const char *key = NULL;

    if (file_islink(file)) {
      key = file_isbroken(file) ? "or" : "ln";
    } else if (file_isdir(file)) {
      key = "di";
      /* TODO: add these (on 2022-09-18) */
      // case f.IsDir() && f.Mode()&os.ModeSticky != 0 && f.Mode()&0002 != 0:
      //       key = "tw"
      // case f.IsDir() && f.Mode()&0002 != 0:
      //         key = "ow"
      // case f.IsDir() && f.Mode()&os.ModeSticky != 0:
      //           key = "st"
      // case f.Mode()&os.ModeNamedPipe != 0:
      //               key = "pi";
      // case f.Mode()&os.ModeSocket != 0:
      //                 key = "so";
      // case f.Mode()&os.ModeDevice != 0:
      //                   key = "bd";
      // case f.Mode()&os.ModeCharDevice != 0:
      //                     key = "cd";
      // case f.Mode()&os.ModeSetuid != 0:
      //                       key = "su";
      // case f.Mode()&os.ModeSetgid != 0:
    } else if (file_isexec(file)) {
      key = "ex";
    }

    hmap_icon_iter it = hmap_icon_end(&cfg.icon_map);
    if (key) {
      it = hmap_icon_find(&cfg.icon_map, key);
    }

    if (!it.ref && file_ext(file)) {
      it = hmap_icon_find(&cfg.icon_map, file_ext(file));
    }

    if (!it.ref) {
      it = hmap_icon_find(&cfg.icon_map, "fi");
    }

    if (it.ref) {
      // move the corsor to make sure we only print one char
      ncplane_putnstr(n, cstr_size(&it.ref->second), cstr_str(&it.ref->second));
      ncplane_putstr_yx(n, y0, 3, " ");
    } else {
      ncplane_putstr(n, "  ");
    }
  }

  const char *hlsubstr = !zsview_is_empty(highlight) && highlight.str[0]
                             ? strcasestr(file_name_str(file), highlight.str)
                             : NULL;
  const int left_space = ncol - 3 - rightmargin - (cfg.icons ? 2 : 0);
  if (left_space > 0) {
    if (hlsubstr) {
      x += print_highlighted_and_shortened(
          n, file_name_str(file), highlight.str, left_space, !file_isdir(file));
    } else {
      x += print_shortened(n, file_name_str(file), left_space,
                           !file_isdir(file));
    }

    for (; x < ncol - rightmargin - 1; x++) {
      ncplane_putchar(n, ' ');
    }
  }

  if (rightmargin > 0) {
    ncplane_cursor_move_yx(n, y0, ncol - rightmargin);
    if (file_islink(file) && cfg.linkchars_len > 0) {
      ncplane_putstr(n, cfg.linkchars);
      ncplane_putchar(n, ' ');
    }
    ncplane_putstr(n, info);
    ncplane_putchar(n, ' ');
  }
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
}

// TODO: plane_draw_dir and draw_file could really use some work
static void plane_draw_dir(struct ncplane *n, Dir *dir, pathlist *sel,
                           pathlist *load, paste_mode mode, zsview highlight,
                           bool print_info) {
  unsigned int nrow;

  // log_info("erasing %s", dir->name);
  ncplane_erase(n);
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_cursor_move_yx(n, 0, 0);
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);

  if (!dir) {
    return;
  }

  if (dir->error) {
    ncplane_putstr_yx(n, 0, 2, strerror(dir->error));
  } else if (dir->status == DIR_LOADING_DELAYED) {
    // print nothing
  } else if (dir->status == DIR_LOADING_INITIAL) {
    ncplane_putstr_yx(n, 0, 2, "loading");
  } else if (dir->length == 0) {
    if (dir->length_all > 0) {
      ncplane_putstr_yx(n, 0, 2, "contains hidden files");
    } else {
      ncplane_putstr_yx(n, 0, 2, "empty");
    }
  } else {
    dir->pos = min(min(dir->pos, nrow - 1), dir->ind);

    uint32_t offset = max(dir->ind - dir->pos, 0);

    if (dir->length <= (uint32_t)nrow) {
      offset = 0;
    }

    const uint32_t l = min(dir->length - offset, nrow);
    for (uint32_t i = 0; i < l; i++) {
      ncplane_cursor_move_yx(n, i, 0);
      draw_file(n, dir->files[i + offset], i == dir->pos, sel, load, mode,
                highlight, print_info, dir->settings.fileinfo);
    }
  }
}
/* }}} */

/* preview {{{ */

static inline void remove_preview(Ui *ui) {
  // ncvisual_blit shrinks the ncplane to approximately fit the image, we
  // need to fix it
  ncplane_resize(ui->planes.preview, 0, 0, 0, 0, 0, 0, ui->preview.y,
                 ui->preview.x);
  ui->preview.preview = NULL;
}

static inline void on_cursor_moved(Ui *ui, bool delay_action);

void ui_update_file_preview(Ui *ui) {
  on_cursor_moved(ui, false);
}

static void on_cursor_resting(EV_P_ ev_timer *w, int revents) {
  log_trace("on_cursor_resting revents=%d", revents);

  if (revents != 0) {
    ev_timer_stop(loop, w);
  }

  Lfm *lfm = w->data;
  Ui *ui = &lfm->ui;

  Preview *preview = ui->preview.preview;
  if (preview) {
    if (revents != 0) {
      if (preview->status == PV_LOADING_DELAYED) {
        async_preview_load(&lfm->async, preview);
      } else {
        async_preview_check(&lfm->async, preview);
      }
    }
  }
  ui_redraw(&lfm->ui, REDRAW_PREVIEW);
}

void ui_update_file_preview_delayed(Ui *ui) {
  if (!cfg.preview || ui->num_columns == 1) {
    return;
  }
  on_cursor_moved(ui, true);
}

// check if the dimensions changed and the preview should be reloaded
#define CHECK_DIMS(pv, nrow) (!(pv)->loading && (pv)->reload_height < (int)nrow)

static inline void on_cursor_moved(Ui *ui, bool delay_action) {
  delay_action &= cfg.preview_delay > 0;

  static uint64_t last_time_called = 0;
  uint64_t now = current_millis();
  if (delay_action) {
    // cursor was resting, don't delay
    if (now - last_time_called > cfg.preview_delay) {
      delay_action = false;
    }
  }
  last_time_called = now;

  log_trace("on_cursor_moved delay_action=%d", delay_action);

  File *file = fm_current_file(&to_lfm(ui)->fm);
  Preview *preview = ui->preview.preview;
  bool is_file_preview = file && !file_isdir(file);
  bool is_same_preview = file != NULL && preview != NULL &&
                         cstr_eq(preview_path(preview), file_path(file));

  unsigned int ncol, nrow;
  ncplane_dim_yx(ui->planes.preview, &nrow, &ncol);
  bool dims_changed = preview != NULL && CHECK_DIMS(preview, nrow);

  if (is_same_preview && dims_changed) {
    preview->status = PV_LOADING_DELAYED;
  }

  if (!is_same_preview) {
    remove_preview(ui);
  }

  if (is_file_preview) {
    // gives us the existing preview or a dummy and, depending on
    // delay_action, loads the preview in the background (or checks and
    // reloads it)
    ui->preview.preview = loader_preview_from_path(
        &to_lfm(ui)->loader, cstr_zv(file_path(file)), !delay_action);
  }

  if (delay_action) {
    ev_timer_again(to_lfm(ui)->loop, &ui->preview_load_timer);
  } else {
    ev_invoke(to_lfm(ui)->loop, &ui->preview_load_timer, 0);
  }
}

void ui_drop_cache(Ui *ui) {
  log_debug("ui_drop_cache");
  if (ui->preview.preview) {
    ui->preview.preview = NULL;
  }
  loader_drop_preview_cache(&to_lfm(ui)->loader);
  ui_update_file_preview(ui);
  ui_redraw(ui, REDRAW_CMDLINE | REDRAW_PREVIEW);
}

/* }}} */

static inline void print_message(Ui *ui, zsview msg, bool error) {
  struct ncplane *n = ui->planes.cmdline;
  ncplane_erase(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
  if (zsview_is_empty(msg)) {
    return;
  }
  if (error) {
    ncplane_set_fg_palindex(ui->planes.cmdline, COLOR_RED);
    ncplane_putnstr_yx(ui->planes.cmdline, 0, 0, msg.size, msg.str);
  } else {
    ncplane_set_fg_default(n);
    ncplane_cursor_move_yx(n, 0, 0);
    ncplane_put_str_ansi(n, msg.str);
  }
  notcurses_render(ui->nc);
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
}

static inline void draw_cmdline(Ui *ui) {
  if (to_lfm(ui)->current_mode->input) {
    const uint32_t cursor_pos = cmdline_draw(&ui->cmdline, ui->planes.cmdline);
    notcurses_cursor_enable(ui->nc, ui->y - 1, cursor_pos);
  } else {
    if (ui->running && ui->show_message) {
      const struct message *msg = vec_message_back(&ui->messages);
      print_message(ui, cstr_zv(&msg->text), msg->error);
    } else {
      statusline_draw(ui);
    }
  }
}

static void loading_indicator_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  Ui *ui = w->data;
  Dir *dir = fm_current_dir(&to_lfm(ui)->fm);
  if (dir->last_loading_action > 0 &&
      current_millis() - dir->last_loading_action >=
          cfg.loading_indicator_delay) {
    ui_redraw(ui, REDRAW_CMDLINE);
    ev_idle_start(EV_A_ & ui->redraw_watcher);
  }
  if (--ui->loading_indicator_timer_recheck_count == 0) {
    ev_timer_stop(EV_A_ w);
  }
}

void ui_start_loading_indicator_timer(Ui *ui) {
  if (cfg.loading_indicator_delay > 0) {
    if (ui->loading_indicator_timer_recheck_count >= 3) {
      return;
    }
    if (ui->loading_indicator_timer_recheck_count++ == 0) {
      ui->loading_indicator_timer_recheck_count++;
      double delay = ((cfg.loading_indicator_delay + 10) / 2) / 1000.;
      ui->loading_indicator_timer.data = ui;
      ev_timer_init(&ui->loading_indicator_timer, loading_indicator_timer_cb, 0,
                    delay);
      ev_timer_again(to_lfm(ui)->loop, &ui->loading_indicator_timer);
    }
  }
}

#undef CHECK_DIMS
