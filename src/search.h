#pragma once

#include <stdbool.h>

struct lfm_s;

// Start a search for `string`. `NULL` or an empty string disables
// highlighting. Does not move the cursor.
void search(struct lfm_s *lfm, const char *string, bool forward);

// Go to next search result in the direction of the current search.
void search_next(struct lfm_s *lfm, bool inclusive);

// Go to previous search result in the direction of the current search.
void search_prev(struct lfm_s *lfm, bool inclusive);

// Disable highlighting of current search results.
void search_nohighlight(struct lfm_s *lfm);
