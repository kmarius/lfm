#pragma once

// Hook type definitions (can be included without dependencies)

#include "defs.h"

#include <string.h>

typedef enum {
  LFM_HOOK_RESIZED = 0,
  LFM_HOOK_ENTER,
  LFM_HOOK_EXITPRE,
  LFM_HOOK_CHDIRPRE,
  LFM_HOOK_CHDIRPOST,
  LFM_HOOK_PASTEBUF,
  LFM_HOOK_SELECTION,
  LFM_HOOK_DIRLOADED,
  LFM_HOOK_DIRUPDATED,
  LFM_HOOK_MODECHANGED,
  LFM_HOOK_FOCUSGAINED,
  LFM_HOOK_FOCUSLOST,
  LFM_HOOK_EXECPRE,
  LFM_HOOK_EXECPOST,
  LFM_NUM_HOOKS
} lfm_hook_id;

extern const char *hook_str[LFM_NUM_HOOKS];

struct Lfm;

void lfm_hooks_init(struct Lfm *lfm);

void lfm_hooks_deinit(struct Lfm *lfm);

// Returns an id with which it can be removed later
i32 lfm_add_hook(struct Lfm *lfm, lfm_hook_id hook, i32 ref);

// Returns the reference of the callback, or 0 if no hook was removed.
i32 lfm_remove_hook(struct Lfm *lfm, i32 id);

// apply all changes made to hooks during a hook callback
void lfm_apply_hook_changes(struct Lfm *lfm);

// returns -1 for invalid hook name
static inline lfm_hook_id hook_name_to_id(const char *name) {
  for (i32 i = 0; i < LFM_NUM_HOOKS; i++) {
    if (strcmp(name, hook_str[i]) == 0) {
      return i;
    }
  }
  return -1;
}
