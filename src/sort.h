#pragma once

#include "dir_settings.h" // sorttype enum and sorttype_str

#include <stddef.h>

i64 compare_name(const void *a, const void *b);

i64 compare_size(const void *a, const void *b);

i64 compare_natural(const void *a, const void *b);

i64 compare_ctime(const void *a, const void *b);

i64 compare_atime(const void *a, const void *b);

i64 compare_mtime(const void *a, const void *b);

i64 compare_key(const void *a, const void *b);
