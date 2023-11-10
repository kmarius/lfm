#pragma once

struct lfm_s;

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

void lfm_hooks_init(struct lfm_s *lfm);

void lfm_hooks_deinit(struct lfm_s *lfm);

// Returns an id with which it can be removed later
int lfm_add_hook(struct lfm_s *lfm, lfm_hook_id hook, int ref);

// Returns the reference of the callback, or 0 if no hook was removed.
int lfm_remove_hook(struct lfm_s *lfm, int id);

void lfm_run_hook(struct lfm_s *lfm, lfm_hook_id hook);

void lfm_run_hook1(struct lfm_s *lfm, lfm_hook_id hook, const char *arg1);
