#include <lauxlib.h>
#include <lua.h>
#include <stddef.h>
#include <string.h>

#include "cvector.h"
#include "hooks.h"
#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "util.h"

// we fold the hook_id and the ref into an id that is returned to the user
//     id == (hook_id << 20) | ref

#define REF_BITS 20
#define REF_MASK ((1 << REF_BITS) - 1)

// absolutely make sure this is the same order as the enum
const char *hook_str[LFM_NUM_HOOKS] = {
    [LFM_HOOK_RESIZED] = LFM_HOOK_NAME_RESIZED,
    [LFM_HOOK_ENTER] = LFM_HOOK_NAME_ENTER,
    [LFM_HOOK_EXITPRE] = LFM_HOOK_NAME_EXITPRE,
    [LFM_HOOK_CHDIRPRE] = LFM_HOOK_NAME_CHDIRPRE,
    [LFM_HOOK_CHDIRPOST] = LFM_HOOK_NAME_CHDIRPOST,
    [LFM_HOOK_PASTEBUF] = LFM_HOOK_NAME_PASTEBUF,
    [LFM_HOOK_SELECTION] = LFM_HOOK_NAME_SELECTION,
    [LFM_HOOK_DIRLOADED] = LFM_HOOK_NAME_DIRLOADED,
    [LFM_HOOK_DIRUPDATED] = LFM_HOOK_NAME_DIRUPDATED,
    [LFM_HOOK_MODECHANGED] = LFM_HOOK_NAME_MODECHANGED,
    [LFM_HOOK_FOCUSGAINED] = LFM_HOOK_NAME_FOCUSGAINED,
    [LFM_HOOK_FOCUSLOST] = LFM_HOOK_NAME_FOCUSLOST,
    [LFM_HOOK_EXECPRE] = LFM_HOOK_NAME_EXECPRE,
    [LFM_HOOK_EXECPOST] = LFM_HOOK_NAME_EXECPOST};

void lfm_hooks_init(Lfm *lfm) {
  memset(lfm->hook_refs, 0, sizeof lfm->hook_refs);
}

void lfm_hooks_deinit(Lfm *lfm) {
  for (int i = 0; i < LFM_NUM_HOOKS; i++) {
    cvector_free(lfm->hook_refs[i]);
  }
}

int lfm_add_hook(Lfm *lfm, lfm_hook_id hook, int ref) {
  cvector_push_back(lfm->hook_refs[hook], ref);
  return (hook << REF_BITS) | ref;
}

int lfm_remove_hook(Lfm *lfm, int id) {
  int ref = id & REF_MASK;
  int hook = id >> REF_BITS;
  if (hook < 0 || hook >= LFM_NUM_HOOKS) {
    // invalid id
    return 0;
  }
  int *hooks = lfm->hook_refs[hook];
  for (size_t i = 0; i < cvector_size(hooks); i++) {
    if (hooks[i] == ref) {
      cvector_swap_erase(hooks, i);
      return ref;
    }
  }
  // no hook
  return 0;
}

void lfm_run_hook(Lfm *lfm, lfm_hook_id hook) {
  log_trace("running hook: %s", hook_str[hook]);
  cvector_foreach(int ref, lfm->hook_refs[hook]) {
    llua_call_ref(lfm->L, ref);
  }
}

void lfm_run_hook1(Lfm *lfm, lfm_hook_id hook, const char *arg1) {
  log_trace("running hook: %s %s", hook_str[hook], arg1);
  cvector_foreach(int ref, lfm->hook_refs[hook]) {
    llua_call_ref1(lfm->L, ref, arg1);
  }
}
