#pragma once

#include "dir.h"
#include "stc/cstr.h"

#define i_type dircache
#define i_keypro cstr
#define i_valdrop(p) dir_destroy(*(p))
#define i_no_clone
#define i_val Dir *
#include "stc/hmap.h"
