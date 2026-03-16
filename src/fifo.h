#pragma once

#include "defs.h"

struct Lfm;

// sets LFMFIFO and returns 0 on success, -1 if the fifo could not be created
i32 fifo_init(struct Lfm *lfm);

void fifo_deinit(void);
