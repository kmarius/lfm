#pragma once

#include <stdbool.h>

#include "fm.h"
#include "ui.h"

/* TODO: add some prefix to these functions (on 2022-01-14) */

/*
 * Start a search for `string`. `NULL` or an empty string disables highlighting. Does not move the cursor.
 */
void search(ui_t *ui, const char *string, bool forward);

/*
 * Go to next search result in the direction of the current search.
 */
void search_next(ui_t *ui, Fm *fm, bool inclusive);

/*
 * Go to previous search result in the direction of the current search.
 */
void search_prev(ui_t *ui, Fm *fm, bool inclusive);

/*
 * Disable highlighting of current search results.
 */
void nohighlight(ui_t *ui);
