#pragma once

#include "stc/cstr.h"
#include "stc/zsview.h"

#define i_type hmap_cstr
#define i_key cstr
#define i_keyraw zsview
#define i_keytoraw cstr_zv
#define i_keyfrom cstr_from_zv
#define i_keydrop cstr_drop
#define i_eq zsview_eq
#define i_hash zsview_hash
#define i_valpro cstr
#define i_no_clone
#include "stc/hmap.h"
