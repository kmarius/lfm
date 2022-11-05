#pragma once

#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <stdint.h>

#include "cmdline.h"
#include "cvector.h"
#include "dir.h"
#include "fm.h"
#include "hashtab.h"
#include "keys.h"
#include "preview.h"

#define REDRAW_INFO    1
#define REDRAW_FM      2
#define REDRAW_CMDLINE 4
#define REDRAW_MENU    8
#define REDRAW_PREVIEW 16
#define REDRAW_FULL    (REDRAW_INFO|REDRAW_FM|REDRAW_CMDLINE|REDRAW_MENU|REDRAW_PREVIEW)

struct message_s {
  char *text;
  bool error;
};

typedef struct ui_s {
  // Current terminal dimensions.
  uint32_t nrow;
  uint32_t ncol;

  // Number of panes, including the preview.
  uint32_t ndirs;

  // Indicates whether the UI is running or suspended.
  bool running;

  struct notcurses *nc;
  struct {
    struct ncplane *cmdline;
    struct ncplane *info;
    struct ncplane *menu;
    struct ncplane *preview;
    cvector_vector_type(struct ncplane*) dirs;
  } planes;

  cvector_vector_type(char*) menubuf;
  cvector_vector_type(struct message_s) messages;

  Cmdline cmdline;

  struct {
    Preview *preview;
    unsigned int cols;
    unsigned int rows;
  } preview;

  const char *highlight; /* pointer to search_string, or NULL */
  char *search_string;
  bool search_forward;

  uint32_t redraw;

  bool show_message;
  input_t *keyseq;

  struct lfm_s *lfm;

  char *infoline;
} Ui;

void kbblocking(bool blocking);

void ui_init(Ui *ui, struct lfm_s *lfm);

void ui_recol(Ui *ui);

void ui_deinit(Ui *ui);

void ui_clear(Ui *ui);

void ui_draw(Ui *ui);

static inline void ui_redraw(Ui *ui, uint32_t mode)
{
  ui->redraw |= mode;
}

void ui_error(Ui *ui, const char *format, ...);

void ui_echom(Ui *ui, const char *format, ...);

void ui_verror(Ui *ui, const char *format, va_list args);

void ui_vechom(Ui *ui, const char *format, va_list args);

void ui_menu_show(Ui *ui, cvector_vector_type(char*) vec);

static inline void ui_menu_hide(Ui *ui)
{
  ui_menu_show(ui, NULL);
}

static inline void ui_keyseq_show(Ui *ui, input_t *keyseq)
{
  ui->keyseq = keyseq;
  ui_redraw(ui, REDRAW_CMDLINE);
}

static inline void ui_keyseq_hide(Ui *ui)
{
  ui_keyseq_show(ui, NULL);
}

void ui_cmd_delete(Ui *ui);

void ui_cmd_clear(Ui *ui);

void ui_cmd_prefix_set(Ui *ui, const char *prefix);

void ui_drop_cache(Ui *ui);

void ui_resume(Ui *ui);

void ui_suspend(Ui *ui);

void ui_set_infoline(Ui *ui, const char *line);
