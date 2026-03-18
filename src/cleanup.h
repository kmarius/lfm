#pragma once

/*
 * Register destructors to clean up e.g. static data before program exit.
 */

#define MAX_DTORS 8

typedef void (*dtor)(void);

__attribute__((nonnull)) int add_dtor(dtor);

void call_dtors(void);
