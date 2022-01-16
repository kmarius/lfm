#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "log.h"
#include "util.h"

int min(int i, int j);

int max(int i, int j);

bool hascaseprefix(const char *restrict string, const char *restrict prefix)
{
	while (*prefix != 0) {
		if (tolower(*prefix++) != tolower(*string++))
			return false;
	}

	return true;
}

bool hasprefix(const char *restrict string, const char *restrict prefix)
{
	while (*prefix != 0) {
		if (*prefix++ != *string++)
			return false;
	}

	return true;
}

bool hassuffix(const char *suf, const char *str)
{
	const char *s = strrchr(str, suf[0]);
	return s && strcasecmp(s, suf) == 0;
}

const char *strcaserchr(const char *str, char c)
{
	const char *last = NULL;
	const char *s = str;
	while (*s != 0) {
		if (*s == c)
			last = s;
		s++;
	}
	return last;
}

bool hascasesuffix(const char *suf, const char *str)
{
	const char *s = strcaserchr(str, suf[0]);
	return s && strcasecmp(s, suf) == 0;
}

char *readable_filesize(double size, char *buf)
{
	int16_t i = 0;
	const char *units[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
	while (size > 1024) {
		size /= 1024;
		i++;
	}
	snprintf(buf, sizeof(buf)-1, "%.*f%s", i > 0 ? 1 : 0, size, units[i]);
	return buf;
}

// https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
int msleep(uint32_t msec)
{
	struct timespec ts;
	int res;

	/* if (msec < 0) { */
	/* 	errno = EINVAL; */
	/* 	return -1; */
	/* } */

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	do {
		res = nanosleep(&ts, &ts);
	} while (res != 0 && errno == EINTR);

	return res;
}

uint64_t current_micros(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((uint64_t) tv.tv_sec) * 1000 * 1000 + tv.tv_usec;
}

uint64_t current_millis(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((uint64_t) tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

void mkdir_p(char *path)
{
	char *sep = strrchr(path, '/');
	if (sep && sep != path) {
		*sep = 0;
		mkdir_p(path);
		*sep = '/';
	}
	if (mkdir(path, 0755) && errno != EEXIST)
		log_error("error while trying to create '%s'", path);
}

char *srealpath(const char *p)
{
	static char fullpath[PATH_MAX+1];
	realpath(p, fullpath);
	return fullpath;
}

char *sbasename(const char *p)
{
	static char buf[PATH_MAX+1];
	strncpy(buf, p, sizeof(buf)-1);
	return basename(buf);
}

char *sdirname(const char *p)
{
	static char buf[PATH_MAX+1];
	strncpy(buf, p, sizeof(buf)-1);
	return dirname(buf);
}
