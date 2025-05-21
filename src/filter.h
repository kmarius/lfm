#include <stdbool.h>

#include "file.h"

#define FILTER_TYPE_GENERAL "filter"
#define FILTER_TYPE_FUZZY "fuzzy"
#define FILTER_TYPE_LUA "lua"

typedef struct Filter Filter;

/*
 * Creates a filter that matches substrings case insensitively.
 * `filter` expressions are negated if the string in the pattern begins with
 * `!`, ` ` (space) works as a logical `and`, `|` as logical `or`.
 * Additionally, files can be filtered by size with "s>1M", "s<4k" etc.
 */
Filter *filter_create_sub(zsview filter);

/*
 * Creates a filter fuzzy matches against the filter pattern.
 * Additionally sets the score on a file upon match so that files
 * can be sorted with the `filter_cmp` compare function.
 */
Filter *filter_create_fuzzy(zsview filter);

/*
 * Creates a filter that calls the lua function with reference `ref` with the
 * file name. `L` is the lua_State.
 */
Filter *filter_create_lua(int ref, void *L);

/*
 * Destroy a filter object.
 */
void filter_destroy(Filter *filter);

/*
 * Match the filter against a file.
 */
bool filter_match(const Filter *filter, const File *file);

/*
 * Get the pattern string used to create the filter.
 */
zsview filter_string(const Filter *filter);

/*
 * Get a string representing the type of the filter, currently either
 * "filter", "fuzzy", or "lua".
 */
const char *filter_type(const Filter *filter);

/*
 * Get the compare function of a filter, if it has one. Returns NULL otherwise.
 */
__compar_fn_t filter_cmp(const Filter *filter);
