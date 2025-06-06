#pragma once

#include "cmdline.h"
#include "preview.h"
#include "trie.h"

#include <ev.h>
#include <notcurses/notcurses.h>

#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include "stc/types.h"
declare_vec(vec_ncplane, struct ncplane *);

struct message {
  cstr text;
  bool error;
};

#define i_type vec_message, struct message
#define i_keydrop(p) (cstr_drop(&p->text))
#define i_no_clone
#include "stc/vec.h"

#define i_TYPE vec_input, input_t
#include "stc/vec.h"

#define REDRAW_INFO 1
#define REDRAW_CMDLINE 2
#define REDRAW_MENU 4
#define REDRAW_PREVIEW 8
#define REDRAW_FM 16
#define REDRAW_CURRENT 32
#define REDRAW_FULL                                                            \
  (REDRAW_INFO | REDRAW_FM | REDRAW_CMDLINE | REDRAW_MENU | REDRAW_PREVIEW)

typedef struct Ui {
  // Indicates whether the UI is running or suspended.
  bool running;

  // Current terminal dimensions.
  uint32_t y, x;

  struct winsize winsize;
  uint16_t ypixel_cell, xpixel_cell;

  // Number of ncplanes, including the preview.
  uint32_t num_columns;

  // notcurses state and ncplanes
  struct notcurses *nc;
  struct {
    struct ncplane *cmdline;
    struct ncplane *info;
    struct ncplane *menu;
    struct ncplane *preview;
    vec_ncplane dirs; // all planes from right to left, including preview
  } planes;

  uint32_t redraw; // Bitfield indicating which components need to be drawn, see
                   // REDRAW_*
  ev_idle redraw_watcher;
  ev_timer loading_indicator_timer;
  int loading_indicator_timer_recheck_count;

  ev_io input_watcher;
  struct {
    struct Trie *cur;       // current leaf in the trie of the active mode
    struct Trie *cur_input; // current leaf in the trie of input maps
    vec_input seq;          // current key sequence
    int count;
    bool accept_count;
    Trie *input;
    Trie *normal;
  } maps;

  // "hidden" under statusline if inactive
  Cmdline cmdline;

  struct {
    Preview *preview;
    unsigned int y, x; // dimensions of the preview ncplane
    bool hidden;       // temporarily hidden
  } preview;

  vec_cstr menubuf;
  bool menu_visible;
  ev_timer menu_delay_timer;
  ev_timer map_clear_timer;
  ev_timer map_suggestion_timer;
  ev_timer preview_load_timer;

  vec_message messages;
  bool show_message; // if true, the latest message is drawn over the statusline
                     // at the bottom

  zsview highlight; /* pointer to search_string, or empty */
  cstr search_string;
  bool search_forward;
} Ui;

void kbblocking(bool blocking);

void ui_init(Ui *ui);

void ui_recol(Ui *ui);

void ui_deinit(Ui *ui);

void ui_on_resize(Ui *ui);

void ui_clear(Ui *ui);

void ui_draw(Ui *ui);

void ui_update_file_preview(Ui *ui);
void ui_update_file_preview_delayed(Ui *ui);

static inline void ui_redraw(Ui *ui, uint32_t mode) {
  ui->redraw |= mode;
}

void ui_error(Ui *ui, const char *format, ...);

void ui_echom(Ui *ui, const char *format, ...);

void ui_verror(Ui *ui, const char *format, va_list args);

void ui_vechom(Ui *ui, const char *format, va_list args);

// takes ownership ov vec, if passed
void ui_menu_show(Ui *ui, vec_cstr *vec, uint32_t delay);

static inline void ui_menu_hide(Ui *ui) {
  ui_menu_show(ui, NULL, 0);
}

void ui_drop_cache(Ui *ui);

void ui_resume(Ui *ui);

void ui_suspend(Ui *ui);

void ui_start_loading_indicator_timer(Ui *ui);
