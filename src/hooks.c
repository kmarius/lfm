#include <lauxlib.h>
#include <lua.h>
#include <string.h>

#include "cvector.h"
#include "hooks.h"
#include "lfm.h"
#include "lua/lfmlua.h"
#include "util.h"

void lfm_hooks_init(Lfm *lfm) {
  memset(lfm->hook_refs, 0, sizeof lfm->hook_refs);
}

void lfm_hooks_deinit(Lfm *lfm) {
  for (int i = 0; i < LFM_NUM_HOOKS; i++) {
    cvector_free(lfm->hook_refs[i]);
  }
}

void lfm_add_hook(Lfm *lfm, lfm_hook_id hook, int ref) {
  cvector_push_back(lfm->hook_refs[hook], ref);
}

void lfm_run_hook(Lfm *lfm, lfm_hook_id hook) {
  cvector_foreach(int ref, lfm->hook_refs[hook]) {
    llua_call_ref(lfm->L, ref);
  }
}

void lfm_run_hook1(Lfm *lfm, lfm_hook_id hook, const char *arg1) {
  cvector_foreach(int ref, lfm->hook_refs[hook]) {
    llua_call_ref1(lfm->L, ref, arg1);
  }
}
