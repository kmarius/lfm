#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <string.h>

#ifndef streq
#define streq(X, Y) (strcmp(X, Y) == 0)
#endif

#ifndef snprintf_nowarn
#define snprintf_nowarn(...) (snprintf(__VA_ARGS__) < 0 ? abort() : (void)0)
#endif

int min(int i, int j);

int max(int i, int j);

bool hasprefix(const char *pre, const char *str);

bool hascaseprefix(const char *pre, const char *str);

int msleep(long msec);

unsigned long current_micros(void);

/* this might have been long long at some point */
unsigned long current_millis(void);

/* recursive mkdir */
void mkdir_p(char *path);

#endif
