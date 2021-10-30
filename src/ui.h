#ifndef UI_H
#define UI_H

#include <lua.h>
#include <notcurses/notcurses.h>

#include "cache.h"
#include "cmdline.h"
#include "cvector.h"
#include "dir.h"
#include "history.h"
#include "fm.h"
#include "preview.h"

#define PREVIEW_CACHE_SIZE 31

typedef struct ui_t {
	int nrow;
	int ncol;

	int ndirs; /* number of columns including the preview */

	fm_t *fm;

	/* needed to get input in app.c */
	int input_ready_fd;
	struct notcurses *nc;

	struct ncplane *plane_cmdline;
	struct ncplane *infoline;
	struct ncplane *menu;
	cvector_vector_type(struct ncplane*) wdirs;

	cvector_vector_type(char*) menubuf;

	preview_t *file_preview;
	cache_t previewcache;

	cmdline_t cmdline;
	history_t history;

	cvector_vector_type(char*) messages;

	char *highlight; /* search */
	bool highlight_active;
	bool search_forward;

	struct {
		unsigned int info : 1;
		unsigned int fm : 1;
		unsigned int cmdline : 1;
		unsigned int menu : 1;
		unsigned int preview : 1;
	} redraw;
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

void ui_showmenu(ui_t *ui, char **vec, int len);

void ui_cmd_clear(ui_t *ui);

void ui_cmd_delete(ui_t *ui);

void ui_cmd_delete_right(ui_t *ui);

void ui_cmd_insert(ui_t *ui, const char *key);

void ui_cmd_prefix_set(ui_t *ui, const char *prefix);

const char *ui_cmdline_get(ui_t *ui);

void ui_cmdline_set(ui_t *ui, const char *line);

void ui_cmd_left(ui_t *ui);

void ui_cmd_right(ui_t *ui);

void ui_cmd_home(ui_t *ui);

void ui_cmd_end(ui_t *ui);

void ui_history_append(ui_t *ui, const char *line);

const char *ui_history_prev(ui_t *ui);

const char *ui_history_next(ui_t *ui);

bool ui_insert_preview(ui_t *ui, preview_t *pv);

void ui_search_nohighlight(ui_t *ui);

void ui_drop_cache(ui_t *ui);

/*
 * Pass NULL as search string to re-enable highlighting with the previous search.
 */
void ui_search_highlight(ui_t *ui, const char *search, bool forward);

#endif /* UI_H */
