#include "hooks.h"

#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "macros.h"

#include <lauxlib.h>
#include <lua.h>

#include <string.h>

struct hook_change {
  lfm_hook_id hook;
  int ref;
  bool remove;
};

#define i_declared
#define i_type vec_hook_change, struct hook_change
#include "stc/vec.h"

// we fold the hook_id and the ref into an id that is returned to the user
//     id == (hook_id << 20) | ref
// TODO: we should probably explicitly use u32 instead of int

#define REF_BITS 20
#define REF_MASK ((1 << REF_BITS) - 1)

// absolutely make sure this is the same order as the enum
const char *hook_str[LFM_NUM_HOOKS] = {
    [LFM_HOOK_RESIZED] = "on_resize",
    [LFM_HOOK_ENTER] = "on_start",
    [LFM_HOOK_EXITPRE] = "on_exit",
    [LFM_HOOK_CHDIRPRE] = "on_chdir_pre",
    [LFM_HOOK_CHDIRPOST] = "on_chdir_post",
    [LFM_HOOK_PASTEBUF] = "on_paste_buffer_change",
    [LFM_HOOK_SELECTION] = "on_selection_change",
    [LFM_HOOK_DIRLOADED] = "on_dir_loaded",
    [LFM_HOOK_DIRUPDATED] = "on_dir_updated",
    [LFM_HOOK_MODECHANGED] = "on_mode_change",
    [LFM_HOOK_FOCUSGAINED] = "on_focus_gained",
    [LFM_HOOK_FOCUSLOST] = "on_focus_lost",
    [LFM_HOOK_EXECPRE] = "on_exec_pre",
    [LFM_HOOK_EXECPOST] = "on_exec_post",
};

void lfm_hooks_init(Lfm *lfm) {
  memset(lfm->hook_refs, 0, sizeof lfm->hook_refs);
  lfm->hook_changes = vec_hook_change_init();
}

void lfm_hooks_deinit(Lfm *lfm) {
  for (int i = 0; i < LFM_NUM_HOOKS; i++) {
    vec_int_drop(&lfm->hook_refs[i]);
  }
  vec_hook_change_drop(&lfm->hook_changes);
}

int lfm_add_hook(Lfm *lfm, lfm_hook_id hook, int ref) {
  if (unlikely(lfm->hook_callback_depth > 0)) {
    struct hook_change change = {
        .hook = hook,
        .ref = ref,
    };
    vec_hook_change_push(&lfm->hook_changes, change);
  } else {
    vec_int_push(&lfm->hook_refs[hook], ref);
  }
  return (hook << REF_BITS) | ref;
}

// TODO: it is currently possible to remove the same ref twice during a callback
int lfm_remove_hook(Lfm *lfm, int id) {
  int ref = id & REF_MASK;
  lfm_hook_id hook = id >> REF_BITS;
  if (hook < 0 || hook >= LFM_NUM_HOOKS) {
    // invalid id
    return 0;
  }
  c_foreach(it, vec_int, lfm->hook_refs[hook]) {
    if (*it.ref == ref) {
      if (unlikely(lfm->hook_callback_depth > 0)) {
        struct hook_change change = {
            .hook = hook,
            .ref = ref,
            .remove = true,
        };
        vec_hook_change_push(&lfm->hook_changes, change);
      } else {
        vec_int_erase_at(&lfm->hook_refs[hook], it);
      }
      return ref;
    }
  }
  c_foreach(it, vec_hook_change, lfm->hook_changes) {
    if (!it.ref->remove && it.ref->hook == hook && it.ref->ref == ref) {
      // invalidate it, don't change the order
      it.ref->ref = 0;
      return ref;
    }
  }
  // no hook
  return 0;
}

void lfm_apply_hook_changes(Lfm *lfm) {
  if (likely(lfm->hook_callback_depth == 0)) {
    c_foreach(it, vec_hook_change, lfm->hook_changes) {
      lfm_hook_id hook = it.ref->hook;
      int ref = it.ref->ref;
      if (ref == 0)
        continue; // invalidated
      if (it.ref->remove) {
        c_foreach(it2, vec_int, lfm->hook_refs[hook]) {
          if (*it2.ref == ref) {
            vec_int_erase_at(&lfm->hook_refs[hook], it2);
            break;
          }
        }
      } else {
        vec_int_push(&lfm->hook_refs[hook], ref);
      }
    }
    vec_hook_change_clear(&lfm->hook_changes);
  }
}
