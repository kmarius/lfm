#include "memory.h"

#define i_implement
#define i_val int
#include "stc/vec.h"

#define i_implement
#define i_type vec_str_o
#define i_val char *
#define i_valraw const char *
#define i_valfrom(p) (strdup(p))
#define i_valto(p) (*p)
#define i_valdrop(p) (xfree(*(p)))
#define i_valclone(p) (strdup(p))
#include "stc/vec.h"
