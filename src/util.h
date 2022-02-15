#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef streq
#define streq(X, Y) (*(char *)(X) == *(char *)(Y) && strcmp(X, Y) == 0)
#endif

#ifndef strcaseeq
#define strcaseeq(X, Y) (strcasecmp(X, Y) == 0)
#endif


static inline char *rtrim(char *s)
{
	char *t = s;
	char *end = s - 1;
	while (*t) {
		if (!isspace(*t))
			end = t;
		t++;
	}
	*++end = 0;
	return s;
}


static inline char *ltrim(char *s)
{
	s--;
	while (isspace(*++s));
	return s;
}


static inline char *trim(char *s)
{
	return ltrim(rtrim(s));
}


static inline int min(int i, int j)
{
	return i < j ? i : j;
}


static inline int max(int i, int j)
{
	return i > j ? i : j;
}

const char *strcasestr(const char *str, const char *sub);

const char *strcaserchr(const char *str, char c);

bool hasprefix(const char *pre, const char *str);

bool hascaseprefix(const char *pre, const char *str);

bool hassuffix(const char *suf, const char *str);

bool hascasesuffix(const char *suf, const char *str);

char *readable_filesize(double size, char *buf);

int msleep(uint32_t msec);

uint64_t current_micros(void);

uint64_t current_millis(void);

/* recursive mkdir */
void mkdir_p(char *path);

int asprintf(char **dst, const char *format, ...);

int vasprintf(char **dst, const char *format, va_list args);

/* these return pointer to statically allocated arrays */
char *srealpath(const char *p);

char *sbasename(const char *p);

char *sdirname(const char *p);

#define arealpath(p) strdup(srealpath(p))

#define abasename(p) strdup(sbasename(p))

#define adirname(p) strdup(sdirname(p))

// Allocates a new path with a beginning ~/ replaced, otherwise a copy of path.
char *path_replace_tilde(const char* path);
