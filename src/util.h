#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef streq
#define streq(X, Y) (*(char *)(X) == *(char *)(Y) && strcmp(X, Y) == 0)
#endif

#ifndef strcaseeq
#define strcaseeq(X, Y) (strcasecmp(X, Y) == 0)
#endif

#ifndef snprintf_nowarn
#define snprintf_nowarn(...) (snprintf(__VA_ARGS__) < 0 ? abort() : (void)0)
#endif

inline int min(int i, int j)
{
	return i < j ? i : j;
}

inline int max(int i, int j)
{
	return i > j ? i : j;
}

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

/* these return pointer to statically allocated arrays */
char *srealpath(const char *p);

char *sbasename(const char *p);

char *sdirname(const char *p);

#define arealpath(p) strdup(srealpath(p))

#define abasename(p) strdup(sbasename(p))

#define adirname(p) strdup(sdirname(p))
