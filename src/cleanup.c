#include "cleanup.h"

#include "defs.h"

#include <stdio.h>
#include <stdlib.h>

static dtor dtors[MAX_DTORS];
static u32 n = 0;

int add_dtor(void (*dtor)()) {
  if (unlikely(n == MAX_DTORS)) {
    fprintf(stderr, "ERROR: too many destructors\n");
    exit(EXIT_FAILURE);
  }
  dtors[n++] = dtor;
  return 0;
}

void call_dtors(void) {
  while (n-- > 0)
    dtors[n]();
}
