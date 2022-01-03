#ifndef SEARCH_H
#define SEARCH_H

#include "fm.h"
#include "ui.h"

/*
 * Start a search for `string`. `NULL` or an empty string disables highlighting. Does not move the cursor.
 */
void search(ui_t *ui, const char *string, bool forward);

/*
 * Go to next search result in the direction of the current search.
 */
void search_next(ui_t *ui, fm_t *fm, bool inclusive);

/*
 * Go to previous search result in the direction of the current search.
 */
void search_prev(ui_t *ui, fm_t *fm, bool inclusive);

/*
 * Disable highlighting of current search results.
 */
void nohighlight(ui_t *ui);

#endif /* SEARCH_H */
