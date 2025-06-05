#pragma once

struct Lfm;

// sets LFMFIFO and returns 0 on success, -1 if the fifo could not be created
int fifo_init(struct Lfm *lfm);

void fifo_deinit(void);
