#include "dir.h"
#include "string.h"

#include "strnatcmp.h"

int compare_name(const void *a, const void *b)
{
	return strcasecmp(((file_t *)a)->name, ((file_t *)b)->name);
}

int compare_size(const void *a, const void *b)
{
	return ((file_t *)a)->stat.st_size - ((file_t *)b)->stat.st_size;
}

int compare_natural(const void *a, const void *b)
{
	return strnatcasecmp(((file_t *)a)->name, ((file_t *)b)->name);
}

int compare_ctime(const void *a, const void *b)
{
	return ((file_t *)b)->stat.st_ctime - ((file_t *)a)->stat.st_ctime;
}
