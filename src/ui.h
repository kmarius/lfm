#pragma once

#include <lua.h>
#include <notcurses/notcurses.h>

#include "cache.h"
#include "cmdline.h"
#include "cvector.h"
#include "dir.h"
#include "fm.h"
#include "history.h"
#include "preview.h"

#define PREVIEW_CACHE_SIZE 63

typedef struct ui_t {
	int nrow;
	int ncol;
	int ndirs; /* number of columns including the preview */

	Fm *fm;

	struct notcurses *nc;
	struct {
		struct ncplane *cmdline;
		struct ncplane *info;
		struct ncplane *menu;
		struct ncplane *preview;
		cvector_vector_type(struct ncplane*) dirs;
	} planes;

	cvector_vector_type(char*) menubuf;
	cvector_vector_type(char*) messages;

	Cmdline cmdline;
	History history;

	struct {
		preview_t *file;
		Cache cache;
	} preview;

	const char *highlight;

	struct {
		bool info : 1;
		bool fm : 1;
		bool cmdline : 1;
		bool menu : 1;
		bool preview : 1;
	} redraw;

	bool message : 1;
} ui_t;

void kbblocking(bool blocking);

void ui_init(ui_t *ui, Fm *fm);

void ui_recol(ui_t *ui);

void ui_deinit(ui_t *ui);

void ui_clear(ui_t *ui);

void ui_draw(ui_t *ui);

void ui_error(ui_t *ui, const char *format, ...);

void ui_echom(ui_t *ui, const char *format, ...);

void ui_verror(ui_t *ui, const char *format, va_list args);

void ui_vechom(ui_t *ui, const char *format, va_list args);

void ui_showmenu(ui_t *ui, cvector_vector_type(char*) vec);

void ui_cmd_clear(ui_t *ui);

void ui_cmd_prefix_set(ui_t *ui, const char *prefix);

bool ui_insert_preview(ui_t *ui, preview_t *pv);

void ui_drop_cache(ui_t *ui);

void ui_notcurses_init(ui_t *ui);

void ui_suspend(ui_t *ui);
