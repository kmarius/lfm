#include <stdbool.h>

typedef struct Filter Filter;

Filter *filter_create(const char *filter);
void filter_destroy(Filter *filter);
bool filter_match(const Filter *filter, const char *str);
const char *filter_string(const Filter *filter);
