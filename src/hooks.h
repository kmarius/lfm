#pragma once

/* TODO: move hook data structures from lua (on 2022-10-09) */

#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"

#define LFM_HOOK_RESIZED "Resized"
#define LFM_HOOK_ENTER "LfmEnter"
#define LFM_HOOK_EXITPRE "ExitPre"
#define LFM_HOOK_CHDIRPRE "ChdirPre"
#define LFM_HOOK_CHDIRPOST "ChdirPost"
#define LFM_HOOK_PASTEBUF "PasteBufChange"
#define LFM_HOOK_DIRLOADED "DirLoaded"
#define LFM_HOOK_DIRUPDATED "DirUpdated"
#define LFM_HOOK_MODECHANGED "ModeChanged"

static inline void lfm_run_hook(Lfm *lfm, const char *hook) {
  log_debug("lfm_run_hook1 %s", hook);
  llua_run_hook(lfm->L, hook);
}

static inline void lfm_run_hook1(struct lfm_s *lfm, const char *hook,
                                 const char *arg1) {
  log_debug("lfm_run_hook1 %s %s", hook, arg1);
  llua_run_hook1(lfm->L, hook, arg1);
}
