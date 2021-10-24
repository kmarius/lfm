#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <string.h>

#ifndef streq
#define streq(X, Y) (strcmp(X, Y) == 0)
#endif

#ifndef strcaseeq
#define strcaseeq(X, Y) (strcasecmp(X, Y) == 0)
#endif

#ifndef snprintf_nowarn
#define snprintf_nowarn(...) (snprintf(__VA_ARGS__) < 0 ? abort() : (void)0)
#endif

int min(int i, int j);

int max(int i, int j);

const char *strcaserchr(const char *str, char c);

bool hasprefix(const char *pre, const char *str);

bool hascaseprefix(const char *pre, const char *str);

bool hassuffix(const char *suf, const char *str);

bool hascasesuffix(const char *suf, const char *str);

int msleep(long msec);

unsigned long current_micros(void);

/* this might have been long long at some point */
unsigned long current_millis(void);

/* recursive mkdir */
void mkdir_p(char *path);

/* these return pointer to statically allocated arrays */
char *srealpath(const char *p);

char *sbasename(const char *p);

char *sdirname(const char *p);

#define arealpath(p) strdup(srealpath(p))

#define abasename(p) strdup(sbasename(p))

#define adirname(p) strdup(sdirname(p))

#endif /* UTIL_H */
