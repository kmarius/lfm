#include <stdlib.h>
#include <string.h>

#include "dir.h"
#include "search.h"

static char *search_string = NULL;
static bool search_forward = true;


/* pass NULL to highlight previous search */
static inline void highlight(Ui *ui, const char *string)
{
	if (string) {
		free(search_string);
		search_string = strdup(string);
	}
	ui->highlight = search_string;
	ui_redraw(ui, REDRAW_FM);
}


// don't remove search_string here
void nohighlight(Ui *ui)
{
	ui->highlight = NULL;
	ui_redraw(ui, REDRAW_FM);
}


inline void search(Ui *ui, const char *string, bool forward)
{
	if (string && string[0] == 0)
		string = NULL;

	if (!string) {
		free(search_string);
		search_string = NULL;
		nohighlight(ui);
	} else {
		search_forward = forward;
		highlight(ui, string);
	}
}


static void search_next_forward(Ui *ui, Fm *fm, bool inclusive)
{
	if (!search_string)
		return;

	Dir *dir = fm_current_dir(fm);
	highlight(ui, NULL);
	for (uint16_t i = inclusive ? 0 : 1; i < dir->length; i++) {
		const uint16_t ind = (dir->ind + i) % dir->length;
		if (strcasestr(file_name(dir->files[ind]), search_string)) {
			fm_cursor_move_to_ind(fm, ind);
			return;
		}
	}
}


static void search_next_backwards(Ui *ui, Fm *fm, bool inclusive)
{
	if (!search_string)
		return;

	Dir *dir = fm_current_dir(fm);
	highlight(ui, NULL);
	for (uint16_t i = inclusive ? 0 : 1 ; i < dir->length; i++) {
		const uint16_t ind = (dir->ind - i + dir->length) % dir->length;
		if (strcasestr(file_name(dir->files[ind]), search_string)) {
			fm_cursor_move_to_ind(fm, ind);
			return;
		}
	}
}


void search_next(Ui *ui, Fm *fm, bool inclusive)
{
	if (search_forward)
		search_next_forward(ui, fm, inclusive);
	else
		search_next_backwards(ui, fm, inclusive);
}


void search_prev(Ui *ui, Fm *fm, bool inclusive)
{
	if (search_forward)
		search_next_backwards(ui, fm, inclusive);
	else
		search_next_forward(ui, fm, inclusive);
}
