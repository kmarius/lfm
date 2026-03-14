#pragma once

#define i_header
#define i_type vec_str, char *
#define i_keyraw const char *
#define i_keyfrom(p) (strdup(p))
#define i_keytoraw(p) (*(p))
#define i_keydrop(p) (free(*p))
#define i_no_clone
#include "stc/vec.h"
