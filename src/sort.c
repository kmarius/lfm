#include "file.h"
#include "strnatcmp.h"


int compare_name(const void *a, const void *b)
{
	return strcasecmp(file_name(*(File **) a), file_name(*(File **) b));
}


int compare_size(const void *a, const void *b)
{
	const int64_t c = file_size(*(File **) a) - file_size(*(File **) b);
	return c < 0 ? -1 : c > 0 ? 1 : 0;
}


int compare_natural(const void *a, const void *b)
{
	return strcasecmp(file_name(*(File **) a), file_name(*(File **) b));
}


int compare_ctime(const void *a, const void *b)
{
	return file_ctime(*(File **) b) - file_ctime(*(File **) a);
}
