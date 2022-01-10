#include "dir.h"
#include "strnatcmp.h"

int compare_name(const void *a, const void *b)
{
	return strcasecmp(((file_t *)a)->name, ((file_t *)b)->name);
}

int compare_size(const void *a, const void *b)
{
	long long c = ((file_t *)a)->lstat.st_size - ((file_t *)b)->lstat.st_size;
	return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int compare_natural(const void *a, const void *b)
{
	return strnatcasecmp((*(file_t **)a)->name, (*(file_t **)b)->name);
}

int compare_ctime(const void *a, const void *b)
{
	return ((file_t *)b)->lstat.st_ctime - ((file_t *)a)->lstat.st_ctime;
}
