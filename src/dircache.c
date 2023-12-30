#include "dir.h"

#define i_header
#include "stc/cstr.h"

#define i_valdrop(p) dir_destroy(*(p))
#define i_no_clone
#define i_implement
#include "dircache.h"
