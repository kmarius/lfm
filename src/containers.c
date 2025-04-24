#include "memory.h"

#define i_implement
#include "stc/cstr.h"

#define i_implement
#define i_type vec_int, int
#include "stc/vec.h"

#define i_implement
#define i_type vec_str, char *
#define i_keyraw const char *
#define i_keyfrom(p) (strdup(p))
#define i_keytoraw(p) (*p)
#define i_keydrop(p) (xfree(*(p)))
#define i_keyclone(p) (strdup(p))
#include "stc/vec.h"
