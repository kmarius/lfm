#include <stdbool.h>

#include "fm.h"
#include "ui.h"

bool find(Fm *fm, Ui *ui, const char *prefix)
{
	Dir *dir = fm_current_dir(fm);
	uint16_t start = dir->ind;
	uint16_t nmatches = 0;
	for (uint16_t i = 0; i < dir->length; i++) {
		if (hascaseprefix(file_name(dir->files[(start + i) % dir->length]), prefix)) {
			if (nmatches == 0) {
				if ((start + i) % dir->length < dir->ind)
					fm_up(fm, dir->ind - (start + i) % dir->length);
				else
					fm_down(fm, (start + i) % dir->length - dir->ind);
				ui->redraw |= REDRAW_FM;
			}
			nmatches++;
		}
	}
	return nmatches == 1;
}
