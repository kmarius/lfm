#include "dir.h"
#include "strnatcmp.h"

int compare_name(const void *a, const void *b)
{
	return strcasecmp(((File *)a)->name, ((File *)b)->name);
}

int compare_size(const void *a, const void *b)
{
	long long c = ((File *)a)->lstat.st_size - ((File *)b)->lstat.st_size;
	return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int compare_natural(const void *a, const void *b)
{
	return strnatcasecmp((*(File **)a)->name, (*(File **)b)->name);
}

int compare_ctime(const void *a, const void *b)
{
	return ((File *)b)->lstat.st_ctime - ((File *)a)->lstat.st_ctime;
}
