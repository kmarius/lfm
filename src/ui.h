#ifndef UI_H
#define UI_H

#include <notcurses/notcurses.h>
#include <lua.h>

#include "cvector.h"
#include "dir.h"
#include "nav.h"
#include "preview.h"
#include "cache.h"
#include "history.h"

#define ACC_SIZE 256
#define PREFIX_SIZE 32
#define PREVIEW_CACHE_SIZE 31

typedef struct ui_t {
	int nrow;
	int ncol;

	int ndirs; /* number of columns including the preview */

	nav_t *nav;

	/* needed to get input in app.c */
	int input_ready_fd;
	struct notcurses *nc;

	struct ncplane *cmdline;
	struct ncplane *infoline;
	struct ncplane *menu;
	cvector_vector_type(struct ncplane*) wdirs;

	char cmd_prefix[PREFIX_SIZE];
	char cmd_acc_left[ACC_SIZE];
	char cmd_acc_right[ACC_SIZE];

	cvector_vector_type(char*) menubuf;

	preview_t *file_preview;
	cache_t *previewcache;

	history_t history;

	cvector_vector_type(char*) messages;

	char *highlight; /* search */
	bool highlight_active;
	bool search_forward;
} ui_t;

void ui_init(ui_t *ui, nav_t *nav);

void ui_resize(ui_t *ui);

void ui_recol(ui_t *ui);

void ui_destroy(ui_t *ui);

void ui_clear(ui_t *ui);

void ui_draw(ui_t *ui);

void ui_draw_dirs(ui_t *ui);

void ui_error(ui_t *ui, const char *format, ...);

void ui_echom(ui_t *ui, const char *format, ...);

void ui_verror(ui_t *ui, const char *format, va_list args);

void ui_vechom(ui_t *ui, const char *format, va_list args);

void ui_showmenu(ui_t *ui, char **vec, int len);

void ui_cmd_clear(ui_t *ui);

void ui_cmd_delete(ui_t *ui);

void ui_cmd_delete_right(ui_t *ui);

void ui_cmd_insert(ui_t *ui, char key);

void ui_cmd_prefix_set(ui_t *ui, const char *prefix);

const char *ui_cmdline_get(const ui_t *ui);

void ui_cmdline_set(ui_t *ui, const char *line);

void ui_cmd_left(ui_t *ui);

void ui_cmd_right(ui_t *ui);

void ui_cmd_home(ui_t *ui);

void ui_cmd_end(ui_t *ui);

void ui_history_append(ui_t *ui, const char *line);

const char *ui_history_prev(ui_t *ui);

const char *ui_history_next(ui_t *ui);

bool ui_insert_preview(ui_t *ui, preview_t *pv);

void ui_draw_preview(ui_t *ui);

void ui_kbblocking(bool blocking);

void ui_search_nohighlight(ui_t *ui);

/*
 * Pass NULL as search string to re-enable highlighting with the previous search.
 */
void ui_search_highlight(ui_t *ui, const char *search, bool forward);

#endif
