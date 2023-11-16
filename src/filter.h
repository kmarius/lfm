#include <stdbool.h>

#include "file.h"

#define FILTER_TYPE_GENERAL "filter"
#define FILTER_TYPE_FUZZY "fuzzy"
#define FILTER_TYPE_LUA "lua"

typedef struct Filter Filter;

Filter *filter_create_sub(const char *filter);
Filter *filter_create_fuzzy(const char *filter);
Filter *filter_create_lua(int ref, void *L);

void filter_destroy(Filter *filter);
bool filter_match(const Filter *filter, const File *file);
const char *filter_string(const Filter *filter);
const char *filter_type(const Filter *filter);
__compar_fn_t filter_sort(const Filter *filter);
