#include <stdbool.h>

#include "fm.h"
#include "ui.h"

static char *find_prefix;


bool find(Fm *fm, Ui *ui, const char *prefix)
{
	find_prefix = strdup(prefix);

	Dir *dir = fm_current_dir(fm);
	uint16_t nmatches = 0;
	for (uint16_t i = dir->ind; i < dir->length + dir->ind; i++) {
		if (hascaseprefix(file_name(dir->files[i % dir->length]), prefix)) {
			if (nmatches == 0) {
				fm_cursor_move_to_ind(fm, i % dir->length);
				ui_redraw(ui, REDRAW_FM);
			}
			nmatches++;
		}
	}
	return nmatches == 1;
}


void find_next(Fm *fm, Ui *ui)
{
	if (!find_prefix)
		return;

	Dir *dir = fm_current_dir(fm);
	for (uint16_t i = 0; i < dir->length; i++) {
		const uint16_t ind = (dir->ind + 1 + i) % dir->length;
		if (hascaseprefix(file_name(dir->files[ind]), find_prefix)) {
			fm_cursor_move_to_ind(fm, ind);
			ui_redraw(ui, REDRAW_FM);
			return;
		}
	}
}


void find_prev(Fm *fm, Ui *ui)
{
	if (!find_prefix)
		return;

	Dir *dir = fm_current_dir(fm);
	for (uint16_t i = 0; i < dir->length; i++) {
		const uint16_t ind = (dir->ind - 1 - i + dir->length) % dir->length;
		if (hascaseprefix(file_name(dir->files[ind]), find_prefix)) {
			fm_cursor_move_to_ind(fm, ind);
			ui_redraw(ui, REDRAW_FM);
			return;
		}
	}
}


void find_clear()
{
	free(find_prefix);
	find_prefix = NULL;
}
