#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <ncurses.h>
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "async.h"
#include "cmdline.h"
#include "config.h"
#include "cvector.h"
#include "dir.h"
#include "file.h"
#include "filter.h"
#include "fm.h"
#include "hashtab.h"
#include "hooks.h"
#include "infoline.h"
#include "input.h"
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "macros.h"
#include "memory.h"
#include "mode.h"
#include "ncutil.h"
#include "statusline.h"
#include "ui.h"
#include "util.h"

#define EXT_MAX_LEN 128 // to convert the extension to lowercase

static void menu_delay_timer_cb(EV_P_ ev_timer *w, int revents);
static void draw_dirs(Ui *ui);
static void plane_draw_dir(struct ncplane *n, Dir *dir, LinkedHashtab *sel,
                           LinkedHashtab *load, paste_mode mode,
                           const char *highlight, bool print_info);
static void draw_preview(Ui *ui);
static void update_preview(Ui *ui);
static void draw_menu(Ui *ui, cvector_vector_type(char *) menu);
static void menu_resize(Ui *ui);
static inline void print_message(Ui *ui, const char *msg, bool error);
static inline void draw_cmdline(Ui *ui);
static void redraw_cb(EV_P_ ev_idle *w, int revents);
static int resize_cb(struct ncplane *n);

/* init/resize {{{ */

void ui_init(Ui *ui) {
  ev_idle_init(&ui->redraw_watcher, redraw_cb);
  ui->redraw_watcher.data = ui;
  ev_idle_start(to_lfm(ui)->loop, &ui->redraw_watcher);

  cmdline_init(&ui->cmdline);
  input_init(to_lfm(ui));
  ui_resume(ui);
}

void ui_deinit(Ui *ui) {
  ui_suspend(ui);
  cvector_foreach_ptr(struct message_s * m, ui->messages) {
    xfree(m->text);
  }
  cvector_free(ui->messages);
  cvector_ffree(ui->menubuf, xfree);
  cmdline_deinit(&ui->cmdline);
  xfree(ui->search_string);
  xfree(ui->infoline);
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
  fm_resize(fm, ui->y - 2);
  menu_resize(ui);
  return 0;
}

void ui_resume(Ui *ui) {
  log_debug("resuming ui");
  kbblocking(false);
  struct notcurses_options ncopts = {
      .flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS |
               NCOPTION_PRESERVE_CURSOR,
  };
  // ui->nc = notcurses_core_init(&ncopts, NULL);
  ui->nc = notcurses_init(&ncopts, NULL);
  if (!ui->nc) {
    exit(EXIT_FAILURE);
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

  opts.resizecb = resize_cb;
  ui->planes.info = ncplane_create(ncstd, &opts);
  opts.resizecb = NULL;

  opts.y = ui->y - 1;
  ui->planes.cmdline = ncplane_create(ncstd, &opts);

  ui_recol(ui);

  opts.rows = opts.cols = 1;
  ui->planes.menu = ncplane_create(ncstd, &opts);
  ncplane_move_bottom(ui->planes.menu);

  ev_timer_init(&ui->menu_delay_timer, menu_delay_timer_cb, 0, 0);
  ui->menu_delay_timer.data = to_lfm(ui);

  input_resume(to_lfm(ui));
  ui_redraw(ui, REDRAW_FM);
  ui->running = true;
}

void ui_suspend(Ui *ui) {
  log_debug("suspending ui");
  ui->running = false;
  input_suspend(to_lfm(ui));
  cvector_ffree_clear(ui->planes.dirs, ncplane_destroy);
  ncplane_destroy(ui->planes.cmdline);
  ncplane_destroy(ui->planes.menu);
  ncplane_destroy(ui->planes.info);
  notcurses_stop(ui->nc);
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
  struct ncplane *ncstd = notcurses_stdplane(ui->nc);

  cvector_fclear(ui->planes.dirs, ncplane_destroy);

  ui->num_columns = cvector_size(cfg.ratios);

  uint32_t sum = 0;
  for (uint32_t i = 0; i < ui->num_columns; i++) {
    sum += cfg.ratios[i];
  }

  struct ncplane_options opts = {
      .y = 1,
      .rows = ui->y > 2 ? ui->y - 2 : 1,
  };

  uint32_t xpos = 0;
  for (uint32_t i = 0; i < ui->num_columns - 1; i++) {
    opts.cols = (ui->x - ui->num_columns + 1) * cfg.ratios[i] / sum;
    if (opts.cols == 0) {
      opts.cols = 1;
    }
    opts.x = xpos;
    cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
    xpos += opts.cols + 1;
  }
  opts.x = xpos;
  opts.cols = ui->x - xpos - 1;
  cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
  ui->planes.preview = ui->planes.dirs[ui->num_columns - 1];
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
  if (ui->redraw & REDRAW_FM) {
    draw_dirs(ui);
  }
  if (ui->redraw & (REDRAW_MENU | REDRAW_MENU)) {
    draw_menu(ui, ui->menubuf);
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
    plane_draw_dir(ui->planes.dirs[l - i - 1], fm->dirs.visible[i],
                   fm->selection.paths, fm->paste.buffer, fm->paste.mode,
                   i == 0 ? ui->highlight : NULL, i == 0);
  }
}

static void draw_preview(Ui *ui) {
  Fm *fm = &to_lfm(ui)->fm;
  if (cfg.preview && ui->num_columns > 1) {
    if (fm->dirs.preview) {
      plane_draw_dir(ui->planes.preview, fm->dirs.preview, fm->selection.paths,
                     fm->paste.buffer, fm->paste.mode, NULL, false);
    } else {
      update_preview(ui);
      if (ui->preview.preview) {
        preview_draw(ui->preview.preview, ui->planes.preview);
      } else {
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

void ui_verror(Ui *ui, const char *format, va_list args) {
  struct message_s msg = {NULL, true};
  vasprintf(&msg.text, format, args);

  log_error(msg.text);

  cvector_push_back(ui->messages, msg);

  ui->show_message = true;
}

void ui_vechom(Ui *ui, const char *format, va_list args) {
  struct message_s msg = {NULL, false};
  vasprintf(&msg.text, format, args);

  cvector_push_back(ui->messages, msg);

  ui->show_message = true;
}

/* }}} */

/* menu {{{ */

/* most notably, replaces tabs with (up to) 8 spaces */
static void draw_menu(Ui *ui, cvector_vector_type(char *) menubuf) {
  if (!menubuf || !ui->menu_visible) {
    return;
  }

  struct ncplane *n = ui->planes.menu;

  ncplane_erase(n);

  /* needed to draw over directories */
  ncplane_set_base(n, " ", 0, 0);

  for (size_t i = 0; i < cvector_size(menubuf); i++) {
    ncplane_cursor_move_yx(n, i, 0);
    const char *str = menubuf[i];
    uint32_t xpos = 0;

    while (*str) {
      const char *start = str;
      while (*str && *str != '\t' && *str != '\033') {
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
  const uint32_t h = max(1, min(cvector_size(ui->menubuf), ui->y - 2));
  ncplane_resize(ui->planes.menu, 0, 0, 0, 0, 0, 0, h, ui->x);
  ncplane_move_yx(ui->planes.menu, ui->y - 1 - h, 0);
  if (ui->menubuf) {
    ncplane_move_top(ui->planes.menu);
  }
}

static void menu_clear(Ui *ui) {
  if (!ui->menubuf) {
    return;
  }

  ncplane_erase(ui->planes.menu);
  ncplane_move_bottom(ui->planes.menu);
}

void ui_menu_show(Ui *ui, cvector_vector_type(char *) vec, uint32_t delay) {
  struct ev_loop *loop = to_lfm(ui)->loop;
  ev_timer_stop(EV_A_ & ui->menu_delay_timer);
  if (ui->menubuf) {
    menu_clear(ui);
    cvector_ffree_clear(ui->menubuf, xfree);
    ui->menu_visible = false;
  }
  if (cvector_size(vec) > 0) {
    ui->menubuf = vec;

    if (delay > 0) {
      ui->menu_delay_timer.repeat = (float)delay / 1000.0;
      ev_timer_again(EV_A_ & ui->menu_delay_timer);
    } else {
      menu_delay_timer_cb(EV_A_ & ui->menu_delay_timer, 0);
    }
  }
  ui_redraw(ui, REDRAW_MENU);
}

static void menu_delay_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  Ui *ui = &lfm->ui;
  if (ui->menubuf) {
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
    uint64_t *chan = ht_get(cfg.colors.color_map, buf);
    if (chan) {
      return *chan;
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
                      LinkedHashtab *sel, LinkedHashtab *load, paste_mode mode,
                      const char *highlight, bool print_info,
                      fileinfo fileinfo) {
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
      strftime(info, sizeof info, cfg.timefmt, tm);
    } break;
    case INFO_CTIME: {
      struct tm *tm = localtime(&file->stat.st_ctim.tv_sec);
      strftime(info, sizeof info, cfg.timefmt, tm);
    } break;
    case INFO_MTIME: {
      struct tm *tm = localtime(&file->stat.st_mtim.tv_sec);
      strftime(info, sizeof info, cfg.timefmt, tm);
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

  if (lht_get(sel, file_path(file))) {
    ncplane_set_channels(n, cfg.colors.selection);
  } else if (mode == PASTE_MODE_MOVE && lht_get(load, file_path(file))) {
    ncplane_set_channels(n, cfg.colors.delete);
  } else if (mode == PASTE_MODE_COPY && lht_get(load, file_path(file))) {
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

    const char *icon = NULL;

    if (key) {
      icon = ht_get(cfg.icon_map, key);
    }

    if (!icon && file_ext(file)) {
      icon = ht_get(cfg.icon_map, file_ext(file));
    }

    if (!icon) {
      icon = ht_get(cfg.icon_map, "fi");
    }

    if (icon) {
      // move the corsor to make sure we only print one char
      ncplane_putstr(n, icon);
      ncplane_putstr_yx(n, y0, 3, " ");
    } else {
      ncplane_putstr(n, "  ");
    }
  }

  const char *hlsubstr =
      highlight && highlight[0] ? strcasestr(file_name(file), highlight) : NULL;
  const int left_space = ncol - 3 - rightmargin - (cfg.icons ? 2 : 0);
  if (left_space > 0) {
    if (hlsubstr) {
      x += print_highlighted_and_shortened(n, file_name(file), highlight,
                                           left_space, !file_isdir(file));
    } else {
      x += print_shortened(n, file_name(file), left_space, !file_isdir(file));
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

static void plane_draw_dir(struct ncplane *n, Dir *dir, LinkedHashtab *sel,
                           LinkedHashtab *load, paste_mode mode,
                           const char *highlight, bool print_info) {
  unsigned int nrow;

  ncplane_erase(n);
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_cursor_move_yx(n, 0, 0);

  if (!dir) {
    return;
  }

  if (dir->error) {
    ncplane_putstr_yx(n, 0, 2, strerror(dir->error));
  } else if (dir->updates == 0) {
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

static inline Preview *load_preview(Ui *ui, File *file) {
  return loader_preview_from_path(&to_lfm(ui)->loader, file_path(file));
}

static inline void reset_preview_plane_size(Ui *ui) {
  // ncvisual_blit shrinks the ncplane to approximately fit the image, we
  // need to fix it
  ncplane_resize(ui->planes.preview, 0, 0, 0, 0, 0, 0, ui->preview.y,
                 ui->preview.x);
}

static void update_preview(Ui *ui) {
  unsigned int ncol, nrow;
  ncplane_dim_yx(ui->planes.preview, &nrow, &ncol);

  File *file = fm_current_file(&to_lfm(ui)->fm);
  if (file && !file_isdir(file)) {
    if (ui->preview.preview) {
      if (streq(ui->preview.preview->path, file_path(file))) {
        if (!ui->preview.preview->loading) {
          if (ui->preview.preview->reload_height < (int)nrow ||
              ui->preview.preview->reload_width < (int)ncol) {
            loader_preview_reload(&to_lfm(ui)->loader, ui->preview.preview);
            ui->preview.preview->loading = true;
          } else {
            if (ui->preview.preview->loadtime + cfg.inotify_delay <=
                current_millis()) {
              async_preview_check(&to_lfm(ui)->async, ui->preview.preview);
            }
          }
        }
      } else {
        reset_preview_plane_size(ui);
        ui->preview.preview = load_preview(ui, file);
        ui_redraw(ui, REDRAW_PREVIEW);
      }
    } else {
      reset_preview_plane_size(ui);
      ui->preview.preview = load_preview(ui, file);
      ui_redraw(ui, REDRAW_PREVIEW);
    }
  } else {
    if (ui->preview.preview) {
      ui->preview.preview = NULL;
      ui_redraw(ui, REDRAW_PREVIEW);
    }
  }
}

void ui_drop_cache(Ui *ui) {
  log_debug("ui_drop_cache");
  if (ui->preview.preview) {
    ui->preview.preview = NULL;
  }
  loader_drop_preview_cache(&to_lfm(ui)->loader);
  update_preview(ui);
  ui_redraw(ui, REDRAW_CMDLINE | REDRAW_PREVIEW);
}

/* }}} */

static inline void print_message(Ui *ui, const char *msg, bool error) {
  struct ncplane *n = ui->planes.cmdline;
  ncplane_erase(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
  if (error) {
    ncplane_set_fg_palindex(ui->planes.cmdline, COLOR_RED);
    ncplane_putstr_yx(ui->planes.cmdline, 0, 0, msg);
  } else {
    ncplane_set_fg_default(n);
    ncplane_cursor_move_yx(n, 0, 0);
    ncplane_addastr(n, msg);
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
      struct message_s *msg = cvector_end(ui->messages) - 1;
      print_message(ui, msg->text, msg->error);
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
