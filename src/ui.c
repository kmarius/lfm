#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <ncurses.h>
#include <notcurses/notcurses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "lfm.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "ncutil.h"
#include "ui.h"
#include "util.h"

#define EXT_MAX_LEN 128  // to convert the extension to lowercase

static void draw_dirs(Ui *ui);
static void plane_draw_dir(struct ncplane *n, Dir *dir, LinkedHashtab *sel,
    LinkedHashtab *load, paste_mode mode, const char *highlight, bool print_sizes);
static void draw_cmdline(Ui *ui);
static void draw_preview(Ui *ui);
static void update_preview(Ui *ui);
static void draw_menu(struct ncplane *n, cvector_vector_type(char *) menu);
static void draw_info(Ui *ui);
static void menu_resize(Ui *ui);
static int print_shortened_w(struct ncplane *n, const wchar_t *name, int name_len, int max_len, bool has_ext);
static inline int shorten_file_name(wchar_t *name, int name_len, int max_len, bool has_ext);
static inline int shorten_path(wchar_t *path, int path_len, int max_len);

/* init/resize {{{ */

static int resize_cb(struct ncplane *n)
{
  Ui *ui = ncplane_userptr(n);
  notcurses_stddim_yx(ui->nc, &ui->nrow, &ui->ncol);
  ncplane_resize(ui->planes.info, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
  ncplane_resize(ui->planes.cmdline, 0, 0, 0, 0, 0, 0, 1, ui->ncol);
  ncplane_move_yx(ui->planes.cmdline, ui->nrow - 1, 0);
  lfm_run_hook(ui->lfm, LFM_HOOK_RESIZED);
  ui_recol(ui);
  fm_resize(&ui->lfm->fm, ui->nrow - 2);
  menu_resize(ui);
  return 0;
}


void ui_resume(Ui *ui)
{
  struct notcurses_options ncopts = {
    .flags = NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS | NCOPTION_PRESERVE_CURSOR,
  };
  // ui->nc = notcurses_core_init(&ncopts, NULL);
  ui->nc = notcurses_init(&ncopts, NULL);
  if (!ui->nc) {
    exit(EXIT_FAILURE);
  }

  struct ncplane *ncstd = notcurses_stdplane(ui->nc);

  ncplane_dim_yx(ncstd, &ui->nrow, &ui->ncol);
  ui->lfm->fm.height = ui->nrow - 2;

  struct ncplane_options opts = {
    .y = 0,
    .x = 0,
    .rows = 1,
    .cols = ui->ncol,
    .userptr = ui,
  };

  opts.resizecb = resize_cb;
  ui->planes.info = ncplane_create(ncstd, &opts);
  opts.resizecb = NULL;

  opts.y = ui->nrow-1;
  ui->planes.cmdline = ncplane_create(ncstd, &opts);

  ui_recol(ui);

  opts.rows = opts.cols = 1;
  ui->planes.menu = ncplane_create(ncstd, &opts);
  ncplane_move_bottom(ui->planes.menu);
  ui->running = true;
}


void ui_suspend(Ui *ui)
{
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
  ui->running = false;
}


void ui_init(Ui *ui, struct lfm_s *lfm)
{
  cmdline_init(&ui->cmdline);

  ui->lfm = lfm;

  ui_resume(ui);

  if (!notcurses_canopen_images(ui->nc)) {
    log_info("can not open images");
  } else {
    log_info("can open images");
  }

  log_info("initialized ui");
}


void ui_deinit(Ui *ui)
{
  ui_suspend(ui);
  cvector_foreach_ptr(struct message_s *m, ui->messages) {
    xfree(m->text);
  }
  cvector_ffree(ui->menubuf, xfree);
  cmdline_deinit(&ui->cmdline);
  xfree(ui->search_string);
  xfree(ui->infoline);
}


void kbblocking(bool blocking)
{
  int val = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (val != -1) {
    fcntl(STDIN_FILENO, F_SETFL, blocking ? val & ~O_NONBLOCK : val | O_NONBLOCK);
  }
}


void ui_recol(Ui *ui)
{
  struct ncplane *ncstd = notcurses_stdplane(ui->nc);

  cvector_fclear(ui->planes.dirs, ncplane_destroy);

  ui->ndirs = cvector_size(cfg.ratios);

  uint32_t sum = 0;
  for (uint32_t i = 0; i < ui->ndirs; i++) {
    sum += cfg.ratios[i];
  }

  struct ncplane_options opts = {
    .y = 1,
    .rows = ui->nrow - 2,
  };

  uint32_t xpos = 0;
  for (uint32_t i = 0; i < ui->ndirs - 1; i++) {
    opts.cols = (ui->ncol - ui->ndirs + 1) * cfg.ratios[i] / sum;
    opts.x = xpos;
    cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
    xpos += opts.cols + 1;
  }
  opts.x = xpos;
  opts.cols = ui->ncol - xpos - 1;
  cvector_push_back(ui->planes.dirs, ncplane_create(ncstd, &opts));
  ui->planes.preview = ui->planes.dirs[ui->ndirs-1];
  ui->preview.cols = opts.cols;
  ui->preview.rows = ui->nrow - 2;
}


/* }}} */

/* main drawing/echo/err {{{ */

void ui_draw(Ui *ui)
{
  if (ui->redraw & REDRAW_FM) {
    draw_dirs(ui);
  }
  if (ui->redraw & (REDRAW_MENU | REDRAW_MENU)) {
    draw_menu(ui->planes.menu, ui->menubuf);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_CMDLINE)) {
    draw_cmdline(ui);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_INFO)) {
    draw_info(ui);
  }
  if (ui->redraw & (REDRAW_FM | REDRAW_PREVIEW)) {
    draw_preview(ui);
  }
  if (ui->redraw) {
    notcurses_render(ui->nc);
  }
  ui->redraw = 0;
}


void ui_clear(Ui *ui)
{
  notcurses_refresh(ui->nc, NULL, NULL);

  notcurses_cursor_enable(ui->nc, 0, 0);
  notcurses_cursor_disable(ui->nc);

  ui_redraw(ui, REDRAW_FULL);
}


static void draw_dirs(Ui *ui)
{
  Fm *fm = &ui->lfm->fm;
  const uint32_t l = fm->dirs.length;
  for (uint32_t i = 0; i < l; i++) {
    plane_draw_dir(ui->planes.dirs[l-i-1],
        fm->dirs.visible[i],
        fm->selection.paths,
        fm->paste.buffer,
        fm->paste.mode,
        i == 0 ? ui->highlight : NULL, i == 0);
  }
}


static void draw_preview(Ui *ui)
{
  Fm *fm = &ui->lfm->fm;
  if (cfg.preview && ui->ndirs > 1) {
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

static inline void print_message(Ui *ui, const char *msg, bool error)
{
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
      ansi_addstr(n, msg);
    }
    notcurses_render(ui->nc);
    ncplane_set_fg_default(n);
    ncplane_set_bg_default(n);
    ncplane_set_styles(n, NCSTYLE_NONE);
}

void ui_echom(Ui *ui, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  ui_vechom(ui, format, args);
  va_end(args);
  ui_redraw(ui, REDRAW_CMDLINE);
}


void ui_error(Ui *ui, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  ui_verror(ui, format, args);
  va_end(args);
  ui_redraw(ui, REDRAW_CMDLINE);
}


void ui_verror(Ui *ui, const char *format, va_list args)
{
  struct message_s msg = {NULL, true};
  vasprintf(&msg.text, format, args);

  log_error(msg.text);

  cvector_push_back(ui->messages, msg);

  ui->show_message = true;
}


void ui_vechom(Ui *ui, const char *format, va_list args)
{
  struct message_s msg = {NULL, false};
  vasprintf(&msg.text, format, args);

  cvector_push_back(ui->messages, msg);

  ui->show_message = true;
}


/* }}} */

/* cmd line {{{ */


void ui_cmd_delete(Ui *ui) {
  if (ui->cmdline.left.len == 0 && ui->cmdline.right.len == 0) {
    ui_cmd_clear(ui);
  } else {
    cmdline_delete(&ui->cmdline);
  }
  ui_redraw(ui, REDRAW_CMDLINE);
}

void ui_cmd_prefix_set(Ui *ui, const char *prefix)
{
  if (!prefix) {
    return;
  }

  ui->show_message = false;
  notcurses_cursor_enable(ui->nc, 0, 0);
  cmdline_prefix_set(&ui->cmdline, prefix);
  ui_redraw(ui, REDRAW_CMDLINE);
}


void ui_cmd_clear(Ui *ui)
{
  cmdline_clear(&ui->cmdline);
  notcurses_cursor_disable(ui->nc);
  ui_menu_show(ui, NULL);
  ui_redraw(ui, REDRAW_CMDLINE | REDRAW_MENU);
}


static char *print_time(time_t time, char *buffer, size_t bufsz)
{
  strftime(buffer, bufsz, "%Y-%m-%d %H:%M:%S", localtime(&time));
  return buffer;
}


static uint32_t int_sz(uint32_t n)
{
  uint32_t i = 1;
  while (n >= 10) {
    i++;
    n /= 10;
  }
  return i;
}

void draw_cmdline(Ui *ui)
{
  if (ui->running && ui->show_message && !cmdline_prefix_get(&ui->cmdline)) {
    struct message_s *msg = cvector_end(ui->messages) - 1;
    print_message(ui, msg->text, msg->error);
    return;
  }

  Fm *fm = &ui->lfm->fm;

  char nums[16];
  char size[32];
  char mtime[32];

  struct ncplane *n = ui->planes.cmdline;

  ncplane_erase(n);
  /* sometimes the color is changed to grey */
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  uint32_t rhs_sz = 0;
  uint32_t lhs_sz = 0;

  if (!cmdline_prefix_get(&ui->cmdline)) {
    const Dir *dir = fm->dirs.visible[0];
    if (dir) {
      const File *file = dir_current_file(dir);
      if (file) {
        if (file_error(file)) {
          lhs_sz = ncplane_printf_yx(n, 0, 0,
              "error: %s",
              strerror(file_error(file)));
        } else {
          lhs_sz = ncplane_printf_yx(n, 0, 0,
              "%s %2.ld %s %s %4s %s%s%s",
              file_perms(file), file_nlink(file),
              file_owner(file), file_group(file),
              file_size_readable(file, size),
              print_time(file_mtime(file), mtime, sizeof mtime),
              file_islink(file) ? " -> " : "",
              file_islink(file) ? file_link_target(file) : "");
        }
      }

      rhs_sz = snprintf(nums, sizeof nums, "%u/%u", dir->length > 0 ? dir->ind + 1 : 0, dir->length);
      ncplane_putstr_yx(n, 0, ui->ncol - rhs_sz, nums);

      // these are drawn right to left
      if (dir->filter) {
        rhs_sz += mbstowcs(NULL, filter_string(dir->filter), 0) + 2 + 1;
        ncplane_set_bg_palindex(n, COLOR_GREEN);
        ncplane_set_fg_palindex(n, COLOR_BLACK);
        ncplane_putchar_yx(n, 0, ui->ncol - rhs_sz, ' ');
        ncplane_putstr(n, filter_string(dir->filter));
        ncplane_putchar(n, ' ');
        ncplane_set_bg_default(n);
        ncplane_set_fg_default(n);
        ncplane_putchar(n, ' ');
      }
      if (fm->paste.buffer->size > 0) {
        if (fm->paste.mode == PASTE_MODE_COPY) {
          ncplane_set_channels(n, cfg.colors.copy);
        } else {
          ncplane_set_channels(n, cfg.colors.delete);
        }

        rhs_sz += int_sz(fm->paste.buffer->size) + 2 + 1;
        ncplane_printf_yx(n, 0, ui->ncol-rhs_sz, " %zu ", fm->paste.buffer->size);
        ncplane_set_bg_default(n);
        ncplane_set_fg_default(n);
        ncplane_putchar(n, ' ');
      }
      if (fm->selection.paths->size > 0) {
        ncplane_set_channels(n, cfg.colors.selection);
        rhs_sz += int_sz(fm->selection.paths->size) + 2 + 1;
        ncplane_printf_yx(n, 0, ui->ncol - rhs_sz, " %zu ", fm->selection.paths->size);
        ncplane_set_bg_default(n);
        ncplane_set_fg_default(n);
        ncplane_putchar(n, ' ');
      }
      if (ui->keyseq) {
        char *str = NULL;
        for (size_t i = 0; i < cvector_size(ui->keyseq); i++) {
          for (const char *s = input_to_key_name(ui->keyseq[i]); *s; s++) {
            cvector_push_back(str, *s);
          }
        }
        cvector_push_back(str, 0);
        rhs_sz += mbstowcs(NULL, str, 0) + 1;
        ncplane_putstr_yx(n, 0, ui->ncol - rhs_sz, str);
        ncplane_putchar(n, ' ');
        cvector_free(str);
      }
      if (lhs_sz + rhs_sz > ui->ncol) {
        ncplane_putwc_yx(n, 0, ui->ncol - rhs_sz - 2, cfg.truncatechar);
        ncplane_putchar(n, ' ');
      }
    }
  } else {
    const uint32_t cursor_pos = cmdline_print(&ui->cmdline, n);
    notcurses_cursor_enable(ui->nc, ui->nrow - 1, cursor_pos);
  }
}

/* }}} */

/* info line {{{ */

// default: %u@%h:%p/%f
static void draw_custom_info(
    Ui *ui,
    const char *user,
    const char *host,
    const char *home)
{
  char buf[1024];
  char *buf_ptr = buf;
  char const *buf_end = buf + sizeof buf - 1;

  // path will be truncated first, then file
  char *path_ptr = NULL;
  char *file_ptr = NULL;
  char *spacer_ptr = NULL;

  for (const char *ptr = ui->infoline; *ptr && buf_ptr < buf_end; ptr++) {
    if (*ptr != '%') {
      *buf_ptr++ = *ptr;
    } else {
      ptr++;
      switch (*ptr) {
        case 0:
          // malformed
          break;
        case 'u':
          buf_ptr += snprintf(buf_ptr, sizeof(buf)-1-(buf_ptr - buf), "%s", user);
          break;
        case 'h':
          buf_ptr += snprintf(buf_ptr, sizeof(buf)-1-(buf_ptr - buf), "%s", host);
          break;
        case 'p':
          path_ptr = buf_ptr;
          *buf_ptr++ = 0;
          break;
        case 'f':
          file_ptr = buf_ptr;
          *buf_ptr++ = 0;
          break;
        case 's':
          spacer_ptr = buf_ptr;
          *buf_ptr++ = 0;
          break;
        case '%':
          *buf_ptr++ = '%';
          break;
        default:
          *buf_ptr++ = '%';
          *buf_ptr++ = *ptr;
      }
    }
  }

  *buf_ptr = 0;

  // length of all static tokens
  int static_len = ansi_mblen(buf);

  if (path_ptr) {
    static_len += ansi_mblen(path_ptr+1);
  }
  if (file_ptr) {
    static_len += ansi_mblen(file_ptr+1);
  }
  if (spacer_ptr) {
    static_len += ansi_mblen(spacer_ptr+1);
  }

  int remaining = ui->ncol - static_len;

  wchar_t *file = NULL;
  int file_len = 0;
  bool file_is_dir = false;
  if (file_ptr) {
    const File* f = fm_current_file(&ui->lfm->fm);
    file = ambstowcs(f ? file_name(f) : "", &file_len);
    file_is_dir = f ? file_isdir(f) : false;
  }

  int path_len = 0;
  wchar_t *path_buf = NULL;  // to xfree later
  wchar_t *path = NULL;      // passed to drawing function, possibly points into path_buf

  if (path_ptr) {
    // prepare path string: replace HOME with ~ and shorten if necessary

    const Dir *dir = fm_current_dir(&ui->lfm->fm);

    path_len = mbstowcs(NULL, dir->path, 0);
    path_buf = xmalloc((path_len + 2) * sizeof *path_buf);  // extra space for trailing '/'
    path_len = mbstowcs(path_buf, dir->path, path_len+1);
    path = path_buf;

    wchar_t *path_buf_ptr = path;
    int path_remaining = remaining - file_len;

    if (hasprefix(dir->path, home)) {
      const int n = mbslen(home);
      path += n - 1;
      path_buf_ptr += n - 1;
      *path_buf_ptr++ = '~';
      path_remaining--;
    }

    if (!dir_isroot(dir)) {
      path_remaining--;  // extra trailing '/'
    }

    const int l = wcslen(path_buf_ptr);
    if (path_remaining < l) {
      shorten_path(path_buf_ptr, l, path_remaining);
    }

    if (!dir_isroot(dir)) {
      path_buf_ptr += wcslen(path_buf_ptr);
      *path_buf_ptr++ = '/';
      *path_buf_ptr = 0;
    }
    path_len = path_buf_ptr - path;
    remaining -= path_buf_ptr - path;
  }

  if (file_ptr) {
    if (remaining < file_len) {
      shorten_file_name(file, file_len, remaining, !file_is_dir);
    }
  }

  struct ncplane *n = ui->planes.info;
  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  ansi_addstr(n, buf);
  if (path_ptr && file_ptr) {
    if (path_ptr < file_ptr) {
      ncplane_putwstr(n, path);
      ansi_addstr(n, path_ptr+1);

      ncplane_putwstr(n, file);
      ansi_addstr(n, file_ptr+1);
    } else {
      ncplane_putwstr(n, file);
      ansi_addstr(n, file_ptr+1);

      ncplane_putwstr(n, path);
      ansi_addstr(n, path_ptr+1);
    }
  } else if (path_ptr) {
    ncplane_putwstr(n, path);
    ansi_addstr(n, path_ptr+1);
  } else if (file_ptr) {
    ncplane_putwstr(n, file);
    ansi_addstr(n, file_ptr+1);
  }

  if (spacer_ptr) {
    unsigned int r;
    ncplane_cursor_yx(n, NULL, &r);
    r = ui->ncol - r;
    while (ncplane_putchar(n, ' ') > 0);
    size_t l = ansi_mblen(spacer_ptr+1);
    if (r >= l) {
      ncplane_cursor_move_yx(n, 0, ui->ncol - l);
      ansi_addstr(n, spacer_ptr+1);
    }
  }

  xfree(path_buf);
  xfree(file);
}


static void draw_info(Ui *ui)
{
  // arbitrary
  static int uid = -1;
  static char user[32] = {0};
  static char host[HOST_NAME_MAX + 1] = {0};
  static char *home;
  static uint32_t home_len;

  if (user[0] == 0) {
    strncpy(user, getenv("USER"), sizeof user - 1);
    gethostname(host, sizeof host);
    home = getenv("HOME");
    home_len = mbstowcs(NULL, home, 0);
    uid = getuid();
  }

  struct ncplane *n = ui->planes.info;

  ncplane_erase(n);

  if (ui->infoline) {
    draw_custom_info(ui, user, host, home);
    return;
  }

  ncplane_set_styles(n, NCSTYLE_BOLD);
  if (uid == 0) {
    ncplane_set_fg_palindex(n, COLOR_RED);
  } else {
    ncplane_set_fg_palindex(n, COLOR_GREEN);
  }
  ncplane_putstr_yx(n, 0, 0, user);
  ncplane_putchar(n, '@');
  ncplane_putstr(n, host);
  ncplane_set_fg_default(n);

  ncplane_set_styles(n, NCSTYLE_NONE);
  ncplane_putchar(n, ':');
  ncplane_set_styles(n, NCSTYLE_BOLD);

  const Dir *dir = fm_current_dir(&ui->lfm->fm);
  const File *file = dir_current_file(dir);
  int path_len, name_len;
  wchar_t *path_ = ambstowcs(dir->path, &path_len);
  wchar_t *path = path_;
  wchar_t *name = NULL;

  // shortening should work fine with ascii only names
  wchar_t *end = path + wcslen(path);
  unsigned int remaining;
  ncplane_cursor_yx(n, NULL, &remaining);
  remaining = ui->ncol - remaining;
  if (file) {
    name = ambstowcs(file_name(file), &name_len);
    remaining -= name_len;
  }
  ncplane_set_fg_palindex(n, COLOR_BLUE);
  if (home && hasprefix(dir->path, home)) {
    ncplane_putchar(n, '~');
    remaining--;
    path += home_len;
  }

  if (!dir_isroot(dir)) {
    remaining--; // printing another '/' later
  }

  /* TODO: check remaining < 0 here (on 2022-10-29) */

  // shorten path components if necessary {}
  while (*path && end - path > remaining) {
    ncplane_putchar(n, '/');
    remaining--;
    wchar_t *next = wcschr(++path, '/');
    if (!next) {
      next = end;
    }

    if (end - next <= remaining) {
      // Everything after the next component fits, we can print some of this one
      const int m = remaining - (end - next) - 1;
      if (m >= 2) {
        wchar_t *print_end = path + m;
        remaining -= m;
        while (path < print_end) {
          ncplane_putwc(n, *(path++));
        }

        if (*path != '/') {
          ncplane_putwc(n, cfg.truncatechar);
          remaining--;
        }
      } else {
        ncplane_putwc(n, *path);
        remaining--;
        path = next;
      }
      path = next;
    } else {
      // print one char only.
      ncplane_putwc(n, *path);
      remaining--;
      path = next;
    }
  }
  ncplane_putwstr(n, path);

  if (!dir_isroot(dir)) {
    ncplane_putchar(n, '/');
  }

  if (file) {
    ncplane_cursor_yx(n, NULL, &remaining);
    remaining = ui->ncol - remaining;
    ncplane_set_fg_default(n);
    print_shortened_w(n, name, name_len, remaining, !file_isdir(file));
  }

  xfree(path_);
  xfree(name);
}

/* }}} */

/* menu {{{ */

/* most notably, replaces tabs with (up to) 8 spaces */
static void draw_menu(struct ncplane *n, cvector_vector_type(char *) menubuf)
{
  if (!menubuf) {
    return;
  }

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
        str = ansi_consoom(n, str);
      } else if (*str == '\t') {
        ncplane_putchar(n, ' ');
        xpos++;
        for (const uint32_t l = ((xpos/8)+1)*8; xpos < l; xpos++) {
          ncplane_putchar(n, ' ');
        }
        str++;
      }
    }
  }
}


static void menu_resize(Ui *ui)
{
  const uint32_t h = max(1, min(cvector_size(ui->menubuf), ui->nrow - 2));
  ncplane_resize(ui->planes.menu, 0, 0, 0, 0, 0, 0, h, ui->ncol);
  ncplane_move_yx(ui->planes.menu, ui->nrow - 1 - h, 0);
  if (ui->menubuf) {
    ncplane_move_top(ui->planes.menu);
  }
}


static void menu_clear(Ui *ui)
{
  if (!ui->menubuf) {
    return;
  }

  ncplane_erase(ui->planes.menu);
  ncplane_move_bottom(ui->planes.menu);
}


void ui_menu_show(Ui *ui, cvector_vector_type(char*) vec)
{
  if (ui->menubuf) {
    menu_clear(ui);
    cvector_ffree_clear(ui->menubuf, xfree);
  }
  if (cvector_size(vec) > 0) {
    ui->menubuf = vec;
    menu_resize(ui);
    ncplane_move_top(ui->planes.menu);
  }
  ui_redraw(ui, REDRAW_MENU);
}

/* }}} */

/* draw_dir {{{ */

static uint64_t ext_channel_get(const char *ext)
{
  char buf[EXT_MAX_LEN];

  if (ext) {
    // lowercase for ascii - good enough for now
    size_t i;
    for (i = 0; ext[i] && i < EXT_MAX_LEN-1; i++) {
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


/* TODO: make the following two functions return the length of the output
 * (and make the callers use it) (on 2022-10-29) */
/* TODO: use these in the default infoline drawer (on 2022-10-29) */

// max_len is not a strict upper bound, but we try to make path as short as possible
// path probably shouldn't end with /
static inline int shorten_path(wchar_t *path, int path_len, int max_len)
{
  wchar_t *ptr = path;
  wchar_t *end = path + path_len;
  if (path_len <= max_len)  {
    return path_len;
  } else if (max_len <= 0) {
    // as short as possible
    while (*path) {
      *ptr++ = '/';
      wchar_t *next = wcschr(++path, '/');
      *ptr++ = *path;
      if (!next) {
        break;
      }
      path = next;
    }
  } else {
    while (*path && end - path > max_len) {
      *ptr++ = '/';
      max_len--;

      wchar_t *next = wcschr(++path, '/');
      if (!next) {
        next = end;
      }

      if (end - next <= max_len) {
        // Everything after the next component fits, we can print some of this one
        const int m = max_len - (end - next) - 1;
        if (m >= 2) {
          const wchar_t *keep = path + m;
          max_len -= m;
          while (path < keep) {
            *ptr++ = *(path++);
          }

          if (*path != '/') {
            *ptr++ = cfg.truncatechar;
            max_len--;
          }
        } else {
          *ptr++ = *path;
          max_len--;
        }
      } else {
        // print one char only.
        *ptr++ = *path;
        max_len--;
      }

      path = next;
    }

    // any leftovers fit
    while (*path) {
      *ptr++ = *path++;
    }
  }

  *ptr = 0;

  return 0;
}

static inline int shorten_file_name(wchar_t *name, int name_len, int max_len, bool has_ext)
{
  if (max_len <= 0) {
    *name = 0;
    return 0;
  }

  const wchar_t *ext = has_ext ? wcsrchr(name, L'.') : NULL;

  if (!ext || ext == name) {
    ext = name + name_len;
  }
  const int ext_len = name_len - (ext - name);

  wchar_t *ptr = name;

  int x = max_len;
  if (name_len <= max_len) {
    // everything fits
    return name_len;
  } else if (max_len > ext_len + 1) {
    // keep extension and as much of the name as possible
    int print_name_ind = max_len - ext_len - 1;
    ptr = name + print_name_ind;
    *ptr++ = cfg.truncatechar;
    while (*ext) {
      *ptr++ = *ext++;
    }
  } else if (max_len >= 5) {
    // keep first char of the name and as mutch of the extension as possible
    const wchar_t *keep = ext + max_len - 2 - 1;
    ptr++;
    *ptr++ = cfg.truncatechar;
    while (ext < keep && *ext) {
      *ptr++ = *ext++;
    }
    *ptr++ = cfg.truncatechar;
  } else if (max_len > 1) {
    wchar_t *name_end = name + max_len - 1;
    ptr = name_end;
    *ptr++ = cfg.truncatechar;
  } else {
    // first char only
    ptr++;
  }
  *ptr = 0;

  return x;
}

static int print_shortened_w(struct ncplane *n, const wchar_t *name, int name_len, int max_len, bool has_ext)
{
  if (max_len <= 0) {
    return 0;
  }

  const wchar_t *ext = has_ext ? wcsrchr(name, L'.') : NULL;

  if (!ext || ext == name) {
    ext = name + name_len;
  }
  const int ext_len = name_len - (ext - name);

  int x = max_len;
  if (name_len <= max_len) {
    // everything fits
    x = ncplane_putwstr(n, name);
  } else if (max_len > ext_len + 1) {
    // print extension and as much of the name as possible
    int print_name_ind = max_len - ext_len - 1;
    const wchar_t *print_name_ptr = name + print_name_ind;
    while (name < print_name_ptr) {
      ncplane_putwc(n, *(name++));
    }
    ncplane_putwc(n, cfg.truncatechar);
    ncplane_putwstr(n, ext);
  } else if (max_len >= 5) {
    // print first char of the name and as mutch of the extension as possible
    ncplane_putwc(n, *(name));
    const wchar_t *ext_end = ext + max_len - 2 - 1;
    ncplane_putwc(n, cfg.truncatechar);
    while (ext < ext_end) {
      ncplane_putwc(n, *(ext++));
    }
    ncplane_putwc(n, cfg.truncatechar);
  } else if (max_len > 1) {
    const wchar_t *name_end = name + max_len - 1;
    while (name < name_end) {
      ncplane_putwc(n, *(name++));
    }
    ncplane_putwc(n, cfg.truncatechar);
  } else {
    ncplane_putwc(n, *name);
  }

  return x;
}


static inline int print_shortened(struct ncplane *n, const char *name, int max_len, bool has_ext)
{
  if (max_len <= 0) {
    return 0;
  }

  int name_len;
  wchar_t *namew = ambstowcs(name, &name_len);
  int ret = print_shortened_w(n, namew, name_len, max_len, has_ext);
  xfree(namew);
  return ret;
}


static int print_highlighted_and_shortened(struct ncplane *n, const char *name, const char *hl, int max_len, bool has_ext)
{
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

  /* TODO: some of these branches can probably be optimized/combined (on 2022-02-18) */
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


static void print_file(struct ncplane *n, const File *file,
    bool iscurrent, LinkedHashtab *sel, LinkedHashtab *load, paste_mode mode,
    const char *highlight, bool print_sizes)
{
  unsigned int ncol, y0;
  unsigned int x = 0;
  char size[16];
  ncplane_dim_yx(n, NULL, &ncol);
  ncplane_cursor_yx(n, &y0, NULL);

  int rightmargin = 0;

  if (print_sizes) {
    if (file_isdir(file)) {
      if (file_dircount(file) < 0) {
        snprintf(size, sizeof size, "?");
      } else {
        snprintf(size, sizeof size, "%d", file_dircount(file));
      }
    } else {
      file_size_readable(file, size);
    }
    rightmargin = strlen(size) + 1;

    if (file_islink(file)) {
      rightmargin += 3; /* " ->" */
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

  const char *hlsubstr = highlight && highlight[0] ? strcasestr(file_name(file), highlight) : NULL;
  const int left_space = ncol - 3 - rightmargin - (cfg.icons ? 2 : 0);
  if (left_space > 0) {
    if (hlsubstr) {
      x += print_highlighted_and_shortened(n, file_name(file), highlight, left_space, !file_isdir(file));
    } else {
      x += print_shortened(n, file_name(file), left_space, !file_isdir(file));
    }

    for (; x < ncol - rightmargin - 1; x++) {
      ncplane_putchar(n, ' ');
    }
  }

  if (rightmargin > 0) {
    ncplane_cursor_move_yx(n, y0, ncol - rightmargin);
    if (file_islink(file)) {
      ncplane_putstr(n, "-> ");
    }
    ncplane_putstr(n, size);
    ncplane_putchar(n, ' ');
  }
  ncplane_set_fg_default(n);
  ncplane_set_bg_default(n);
  ncplane_set_styles(n, NCSTYLE_NONE);
}


static void plane_draw_dir(struct ncplane *n, Dir *dir, LinkedHashtab *sel, LinkedHashtab*load,
    paste_mode mode, const char *highlight, bool print_sizes)
{
  unsigned int nrow;

  ncplane_erase(n);
  ncplane_dim_yx(n, &nrow, NULL);
  ncplane_cursor_move_yx(n, 0, 0);

  if (!dir) {
    return;
  }

  if (dir->error) {
    ncplane_putstr_yx(n, 0, 2, strerror(dir->error));
  } else if (dir_loading(dir)) {
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

    if (dir->length <= (uint32_t) nrow) {
      offset = 0;
    }

    const uint32_t l = min(dir->length - offset, nrow);
    for (uint32_t i = 0; i < l; i++) {
      ncplane_cursor_move_yx(n, i, 0);
      print_file(n, dir->files[i + offset],
          i == dir->pos, sel, load, mode, highlight, print_sizes);
    }
  }
}
/* }}} */

/* preview {{{ */

static inline Preview *load_preview(Ui *ui, File *file)
{
  return loader_preview_from_path(&ui->lfm->loader, file_path(file));
}


static inline void reset_preview_plane_size(Ui *ui)
{
  // ncvisual_blit shrinks the ncplane to approximately fit the image, we
  // need to fix it
  ncplane_resize(ui->planes.preview, 0, 0, 0, 0, 0, 0, ui->preview.rows, ui->preview.cols);
}


static void update_preview(Ui *ui)
{
  unsigned int ncol, nrow;
  ncplane_dim_yx(ui->planes.preview, &nrow, &ncol);

  File *file = fm_current_file(&ui->lfm->fm);
  if (file && !file_isdir(file)) {
    if (ui->preview.preview) {
      if (streq(ui->preview.preview->path, file_path(file))) {
        if (!ui->preview.preview->loading) {
          if (ui->preview.preview->reload_height < (int) nrow
              || ui->preview.preview->reload_width < (int) ncol) {
            async_preview_load(&ui->lfm->async, ui->preview.preview);
            ui->preview.preview->loading = true;
          } else {
            if (ui->preview.preview->loadtime + 500 <= current_millis()) {
              async_preview_check(&ui->lfm->async, ui->preview.preview);
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


void ui_drop_cache(Ui *ui)
{
  log_debug("dropping preview cache");
  if (ui->preview.preview) {
    ui->preview.preview = NULL;
  }
  loader_drop_preview_cache(&ui->lfm->loader);
  update_preview(ui);
  ui_redraw(ui, REDRAW_CMDLINE | REDRAW_PREVIEW);
}

/* }}} */

void ui_set_infoline(Ui *ui, const char *line)
{
  xfree(ui->infoline);
  ui->infoline = line ? strdup(line) : NULL;
  ui_redraw(ui, REDRAW_INFO);
}
