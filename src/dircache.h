#pragma once

#include "dir.h"
#include "stc/cstr.h"

// key is zsview of dir->path and owned by dir
#define i_type dircache
#define i_key zsview
#define i_val Dir *
#define i_valdrop(p) dir_destroy(*(p))
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_no_clone
#include "stc/hmap.h"
