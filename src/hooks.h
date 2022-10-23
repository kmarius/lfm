#pragma once

/* TODO: move hook data structures from lua (on 2022-10-09) */

#include "lfm.h"
#include "lua/lfmlua.h"

#define LFM_HOOK_RESIZED    "Resized"
#define LFM_HOOK_ENTER      "LfmEnter"
#define LFM_HOOK_EXITPRE    "ExitPre"
#define LFM_HOOK_CHDIRPRE   "ChdirPre"
#define LFM_HOOK_CHDIRPOST  "ChdirPost"
#define LFM_HOOK_PASTEBUF   "PasteBufChange"
#define LFM_HOOK_DIRLOADED  "DirLoaded"
#define LFM_HOOK_DIRUPDATED "DirUpdated"

static inline void lfm_run_hook(Lfm *lfm, const char *hook)
{
  lua_run_hook(lfm->L, hook);
}

static inline void lfm_run_hook1(struct lfm_s *lfm, const char *hook, const char* arg1)
{
  lua_run_hook1(lfm->L, hook, arg1);
}
