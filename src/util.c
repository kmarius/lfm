#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "log.h"

int min(int i, int j) { return i < j ? i : j; }

int max(int i, int j) { return i > j ? i : j; }

bool hascaseprefix(const char *restrict string, const char *restrict prefix)
{
	while (*prefix) {
		if (tolower(*prefix++) != tolower(*string++)) {
			return false;
		}
	}

	return true;
}

bool hasprefix(const char *restrict string, const char *restrict prefix)
{
	while (*prefix) {
		if (*prefix++ != *string++) {
			return false;
		}
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
	while (*s)
	{
		if (*s == c) {
			last = s;
		}
		s++;
	}
	return last;
}

bool hascasesuffix(const char *suf, const char *str)
{
	const char *s = strcaserchr(str, suf[0]);
	return s && strcasecmp(s, suf) == 0;
}

// https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
int msleep(long msec)
{
	struct timespec ts;
	int res;

	if (msec < 0) {
		errno = EINVAL;
		return -1;
	}

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	do {
		res = nanosleep(&ts, &ts);
	} while (res && errno == EINTR);

	return res;
}

unsigned long current_micros(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (((unsigned long)tv.tv_sec) * 1000 * 1000) + (tv.tv_usec);
}

unsigned long current_millis(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (((unsigned long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

void mkdir_p(char *path)
{
	char *sep = strrchr(path, '/');
	if (sep && sep != path) {
		*sep = 0;
		mkdir_p(path);
		*sep = '/';
	}
	if (mkdir(path, 0755) && errno != EEXIST) {
		log_error("error while trying to create '%s'", path);
	}
}
