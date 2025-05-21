#pragma once

#include "stc/zsview.h"

#include <stdbool.h>

struct Lfm;

// Start a search for `string`. `NULL` or an empty string disables
// highlighting. Does not move the cursor.
void search(struct Lfm *lfm, zsview string, bool forward);

// Go to next search result in the direction of the current search.
void search_next(struct Lfm *lfm, bool inclusive);

// Go to previous search result in the direction of the current search.
void search_prev(struct Lfm *lfm, bool inclusive);

// Disable highlighting of current search results.
void search_nohighlight(struct Lfm *lfm);
