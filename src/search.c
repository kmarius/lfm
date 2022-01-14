#define _GNU_SOURCE

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "fm.h"
#include "dir.h"
#include "search.h"
#include "ui.h"

static char *search_string = NULL;
static bool search_forward = true;

/* pass NULL to highlight previous search */
static inline void highlight(ui_t *ui, const char *string)
{
	if (string != NULL) {
		free(search_string);
		search_string = strdup(string);
	}
	ui->highlight = search_string;
	ui->redraw.fm = 1;
}

inline void nohighlight(ui_t *ui)
{
	ui->highlight = NULL;
	ui->redraw.fm = 1;
}

inline void search(ui_t *ui, const char *string, bool forward)
{
	if (string == NULL || string[0] == 0) {
		nohighlight(ui);
	} else {
		highlight(ui, string);
		search_forward = forward;
	}
}

static void search_next_forward(ui_t *ui, Fm *fm, bool inclusive)
{
	if (search_string == NULL) {
		return;
	}

	Dir *dir = fm_current_dir(fm);
	highlight(ui, NULL);
	for (uint16_t i = inclusive ? 0 : 1; i < dir->length; i++) {
		if (strcasestr(dir->files[(dir->ind + i) % dir->length]->name, search_string)) {
			fm_move_to_ind(fm, (dir->ind + i) % dir->length );
			return;
		}
	}
}

static void search_next_backwards(ui_t *ui, Fm *fm, bool inclusive)
{
	if (search_string == NULL) {
		return;
	}

	Dir *dir = fm_current_dir(fm);
	highlight(ui, NULL);
	for (uint16_t i = inclusive ? 0 : 1 ; i < dir->length; i++) {
		if (strcasestr(dir->files[(dir->length + dir->ind - i) % dir->length]->name, search_string)) {
			fm_move_to_ind(fm, (dir->length + dir->ind - i) % dir->length );
			return;
		}
	}
}

void search_next(ui_t *ui, Fm *fm, bool inclusive)
{
	if (search_forward) {
		search_next_forward(ui, fm, inclusive);
	} else {
		search_next_backwards(ui, fm, inclusive);
	}
}

void search_prev(ui_t *ui, Fm *fm, bool inclusive)
{
	if (search_forward) {
		search_next_backwards(ui, fm, inclusive);
	} else {
		search_next_forward(ui, fm, inclusive);
	}
}
