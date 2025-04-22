#pragma once

#define i_header
#define i_type vec_int, int
#include "stc/vec.h"

#define i_header
#define i_type vec_str, char *
#define i_keyraw const char *
#define i_keyfrom(p) (strdup(p))
#define i_keytoraw(p) (*(p))
#include "stc/vec.h"
