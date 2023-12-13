#include "dir.h"
static inline void Dir_drop(Dir **dir);

#define i_header
#include "stc/cstr.h"

#define i_valdrop Dir_drop
#define i_no_clone
#define i_implement
#include "dircache.h"

static inline void Dir_drop(Dir **dir) {
  dir_destroy(*dir);
}
