#ifndef UI_H
#define UI_H

#include <lua.h>
#include <notcurses/notcurses.h>

#include "cache.h"
#include "cmdline.h"
#include "cvector.h"
#include "dir.h"
#include "fm.h"
#include "history.h"
#include "preview.h"

#define PREVIEW_CACHE_SIZE 31

typedef struct ui_t {
	int nrow;
	int ncol;
	int ndirs; /* number of columns including the preview */

	fm_t *fm;

	struct notcurses *nc;
	struct {
		struct ncplane *cmdline;
		struct ncplane *info;
		struct ncplane *menu;
		cvector_vector_type(struct ncplane*) dirs;
	} planes;

	cvector_vector_type(char*) menubuf;
	cvector_vector_type(char*) messages;

	cmdline_t cmdline;
	history_t history;

	struct {
		preview_t *file;
		cache_t cache;
	} preview;

	struct {
		char *string; /* search */
		bool active : 1;
		bool forward : 1;
	} search;

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

void ui_init(ui_t *ui, fm_t *fm);

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

void ui_history_append(ui_t *ui, const char *line);

const char *ui_history_prev(ui_t *ui);

const char *ui_history_next(ui_t *ui);

bool ui_insert_preview(ui_t *ui, preview_t *pv);

/*
 * Pass NULL as search string to re-enable highlighting with the previous search.
 */
void ui_search_highlight(ui_t *ui, const char *search, bool forward);

void ui_search_nohighlight(ui_t *ui);

void ui_drop_cache(ui_t *ui);

void ui_notcurses_init(ui_t *ui);

void ui_suspend(ui_t *ui);

#endif /* UI_H */
