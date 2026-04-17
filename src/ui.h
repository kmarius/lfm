#pragma once

#include "cmdline.h"
#include "loop.h"
#include "preview.h"
#include "trie.h"
#include "types/vec_cstr.h"

#include <ev.h>
#include <notcurses/notcurses.h>

#include <sys/ioctl.h>

#include <stc/types.h>
declare_vec(vec_ncplane, struct ncplane *);

struct message {
  cstr text;   // the message text
  bool error;  // errors are shown in red and always logged
  i32 timeout; // timeout in ms, after which the message is cleared (if > 0)
};

#define i_type vec_message, struct message
#define i_keydrop(p) (cstr_drop(&p->text))
#define i_no_clone
#include <stc/vec.h>

#define i_TYPE vec_input, input_t
#include <stc/vec.h>

#define i_TYPE queue_input, input_t
#include <stc/queue.h>

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
  u32 y, x;

  struct winsize winsize;
  u16 ypixel_cell, xpixel_cell;

  // Number of ncplanes, including the preview.
  u32 num_columns;

  // notcurses state and ncplanes
  struct notcurses *nc;
  struct {
    struct ncplane *cmdline;
    struct ncplane *info;
    struct ncplane *menu;
    struct ncplane *preview;
    vec_ncplane dirs; // all planes from right to left, including preview
  } planes;

  u32 redraw; // Bitfield indicating which components need to be drawn, see
              // REDRAW_*
  ev_idle redraw_watcher; // draws when the loop is idle
  ev_timer loading_indicator_timer;
  i32 loading_indicator_timer_recheck_count;

  ev_io input_watcher;
  ev_idle input_buffer_watcher;
  queue_input input_buffer;
  struct {
    struct Trie *cur;       // current leaf in the trie of the active mode
    struct Trie *cur_input; // current leaf in the trie of input maps
    vec_input seq;          // current key sequence
    i32 count;
    bool accept_count;
    Trie *input;
    Trie *normal;
  } maps;

  // "hidden" under statusline if inactive
  Cmdline cmdline;

  struct {
    Preview *preview;
    u32 y, x;    // dimensions of the preview ncplane
    bool hidden; // temporarily hidden to prevent flickering while moving
  } preview;

  vec_cstr menubuf;
  bool menu_visible;
  ev_timer menu_delay_timer;
  ev_timer map_clear_timer;
  ev_timer map_suggestion_timer;
  ev_timer preview_load_timer;
  ev_timer message_clear_timer;

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

void ui_update_preview(Ui *ui, bool immediate);

static inline void ui_redraw(Ui *ui, u32 mode) {
  ui->redraw |= mode;
  ev_idle_start(event_loop, &ui->redraw_watcher);
}

void ui_display_message(Ui *ui, struct message msg);

// takes ownership ov vec, if passed
void ui_menu_show(Ui *ui, vec_cstr *vec, u32 delay);

static inline void ui_menu_hide(Ui *ui) {
  ui_menu_show(ui, NULL, 0);
}

void ui_drop_cache(Ui *ui);

void ui_resume(Ui *ui);

void ui_suspend(Ui *ui);

void ui_start_loading_indicator_timer(Ui *ui);
