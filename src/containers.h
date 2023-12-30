#pragma once

#define i_header
#define i_val int
#include "stc/vec.h"

#define i_header
#define i_type vec_str
#define i_val char *
#define i_valraw const char *
#define i_valfrom(p) (strdup(p))
#define i_valto(p) (*(p))
#include "stc/vec.h"
