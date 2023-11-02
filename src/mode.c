#include "mode.h"
#include "cmdline.h"
#include "fm.h"
#include "hashtab.h"
#include "hooks.h"
#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "trie.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

static void mode_free(void *p);

void normal_on_enter(Lfm *lfm);

void visual_on_enter(Lfm *lfm);
void visual_on_exit(Lfm *lfm);
void visual_on_esc(Lfm *lfm);

void lfm_modes_init(Lfm *lfm) {
  ht_init(&lfm->modes, 8, mode_free);
  lfm_mode_register(lfm, &(struct mode){
                             .name = "normal",
                             .on_enter = normal_on_enter,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = "input",
                             .input = true,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = "visual",
                             .input = false,
                             .on_enter = visual_on_enter,
                             .on_exit = visual_on_exit,
                         });
  struct mode *input = ht_get(&lfm->modes, "input");
  lfm->ui.maps.input = input->maps;
  lfm->current_mode = ht_get(&lfm->modes, "normal");
  lfm->ui.maps.normal = lfm->current_mode->maps;

  // TODO: should be done properly eventually, like we do with input modes
  ((struct mode *)ht_get(&lfm->modes, "visual"))->maps = lfm->ui.maps.normal;
}

void lfm_modes_deinit(Lfm *lfm) {
  // maps belong to normal mode, don't double free
  ((struct mode *)ht_get(&lfm->modes, "visual"))->maps = NULL;
  ht_deinit(&lfm->modes);
}

static void mode_free(void *p) {
  if (p) {
    struct mode *mode = p;
    trie_destroy(mode->maps);
    free(mode->name);
    free(mode->prefix);
    free(mode);
  }
}

void normal_on_enter(Lfm *lfm) {
  cmdline_clear(&lfm->ui.cmdline);
  cmdline_prefix_set(&lfm->ui.cmdline, "");
}

void visual_on_enter(Lfm *lfm) {
  fm_on_visual_enter(&lfm->fm);
}

void visual_on_exit(Lfm *lfm) {
  fm_on_visual_exit(&lfm->fm);
}

int lfm_mode_register(Lfm *lfm, const struct mode *mode) {
  if (ht_get(&lfm->modes, mode->name) != NULL) {
    return 1;
  }
  struct mode *newmode = xcalloc(1, sizeof *newmode);
  memcpy(newmode, mode, sizeof *newmode);
  newmode->name = strdup(mode->name);
  if (newmode->prefix) {
    newmode->prefix = strdup(newmode->prefix);
  }
  newmode->maps = trie_create();
  ht_set(&lfm->modes, newmode->name, newmode);
  return 0;
}

int lfm_mode_enter(Lfm *lfm, const char *name) {
  struct mode *mode = ht_get(&lfm->modes, name);
  if (!mode || mode == lfm->current_mode) {
    return 1;
  }

  mode_on_exit(lfm->current_mode, lfm);
  lfm->current_mode = mode;
  mode_on_enter(mode, lfm);

  if (mode->input && mode->prefix) {
    cmdline_prefix_set(&lfm->ui.cmdline, mode->prefix);
  }
  lfm->ui.maps.cur_input = NULL;
  lfm_run_hook1(lfm, LFM_HOOK_MODECHANGED, mode->name);

  ui_redraw(&lfm->ui, REDRAW_INFO);
  return 0;
}

int lfm_mode_exit(Lfm *lfm, const char *name) {
  if (streq(lfm->current_mode->name, name)) {
    return lfm_mode_enter(lfm, "normal");
  }
  return 1;
}

void mode_on_enter(struct mode *mode, Lfm *lfm) {
  if (mode->on_enter) {
    mode->on_enter(lfm);
  } else if (mode->on_enter_ref) {
    llua_call_ref(lfm->L, mode->on_enter_ref);
  }
}

void mode_on_return(struct mode *mode, struct lfm_s *lfm, const char *line) {
  if (mode->on_return) {
    mode->on_return(lfm, line);
  } else if (mode->on_return_ref) {
    llua_call_ref1(lfm->L, mode->on_return_ref, line);
  }
}

void mode_on_change(struct mode *mode, Lfm *lfm) {
  if (mode->on_change) {
    mode->on_change(lfm);
  } else if (mode->on_change_ref) {
    llua_call_ref(lfm->L, mode->on_change_ref);
  }
}

void mode_on_esc(struct mode *mode, Lfm *lfm) {
  if (mode->on_esc) {
    mode->on_esc(lfm);
  } else if (mode->on_esc_ref) {
    llua_call_ref(lfm->L, mode->on_esc_ref);
  }
}

void mode_on_exit(struct mode *mode, Lfm *lfm) {
  if (mode->on_exit) {
    mode->on_exit(lfm);
  } else if (mode->on_exit_ref) {
    llua_call_ref(lfm->L, mode->on_exit_ref);
  }
}
