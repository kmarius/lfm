#pragma once

/*
 * We put the PWD behind a lock so that it can be safely used from other (lua)
 * threads.
 */

#include "defs.h"

#include <stc/zsview.h>

void setpwd(const char *path);

// returns -1 if the buffer is too short, otherwise the length
isize getpwd_buf(char *buf, usize bufzs);

// access with manual unlocking
const char *getpwd_manual_unlock();
zsview getpwd_zv_manual_unlock();
void getpwd_unlock();
