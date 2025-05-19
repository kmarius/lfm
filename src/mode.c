#include "mode.h"

#include "cmdline.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "lua/lfmlua.h"
#include "trie.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

static void normal_on_enter(Lfm *lfm);

static void visual_on_enter(Lfm *lfm);
static void visual_on_exit(Lfm *lfm);

void lfm_modes_init(Lfm *lfm) {
  lfm->modes = hmap_modes_init();
  lfm_mode_register(lfm, &(struct mode){
                             .name = (char *)"normal",
                             .on_enter = normal_on_enter,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = (char *)"input",
                             .input = true,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = (char *)"visual",
                             .input = false,
                             .on_enter = visual_on_enter,
                             .on_exit = visual_on_exit,
                         });
  const struct mode *input = hmap_modes_at(&lfm->modes, "input");
  lfm->ui.maps.input = input->maps;
  lfm->current_mode = hmap_modes_at_mut(&lfm->modes, "normal");
  lfm->ui.maps.normal = lfm->current_mode->maps;

  // TODO: should be done properly eventually, like we do with input modes
  struct mode *visual = hmap_modes_at_mut(&lfm->modes, "visual");
  trie_destroy(visual->maps);
  visual->maps = lfm->ui.maps.normal;
}

void lfm_modes_deinit(Lfm *lfm) {
  // these maps belong to normal mode, don't double free
  hmap_modes_at_mut(&lfm->modes, "visual")->maps = NULL;
  hmap_modes_drop(&lfm->modes);
}

static void normal_on_enter(Lfm *lfm) {
  cmdline_clear(&lfm->ui.cmdline);
  cmdline_prefix_set(&lfm->ui.cmdline, "");
}

static void visual_on_enter(Lfm *lfm) {
  fm_on_visual_enter(&lfm->fm);
  ui_redraw(&lfm->ui, REDRAW_FM);
}

static void visual_on_exit(Lfm *lfm) {
  fm_on_visual_exit(&lfm->fm);
}

int lfm_mode_register(Lfm *lfm, const struct mode *mode) {
  if (hmap_modes_contains(&lfm->modes, mode->name)) {
    return 1;
  }
  // modes might change when if the table is resized
  char *current = lfm->current_mode ? lfm->current_mode->name : NULL;
  hmap_modes_result res = hmap_modes_emplace(&lfm->modes, mode->name, *mode);
  res.ref->first = res.ref->second.name;
  if (current) {
    lfm->current_mode = hmap_modes_at_mut(&lfm->modes, current);
  }
  return 0;
}

int lfm_mode_enter(Lfm *lfm, const char *name) {
  hmap_modes_iter it = hmap_modes_find(&lfm->modes, name);
  if (!it.ref || &it.ref->second == lfm->current_mode) {
    return 1;
  }
  struct mode *mode = &it.ref->second;

  mode_on_exit(lfm->current_mode, lfm);
  lfm->current_mode = mode;
  mode_on_enter(mode, lfm);

  if (mode->input && mode->prefix) {
    cmdline_prefix_set(&lfm->ui.cmdline, mode->prefix);
  }
  lfm->ui.maps.cur_input = NULL;
  lfm_run_hook(lfm, LFM_HOOK_MODECHANGED, mode->name);

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

void mode_on_return(struct mode *mode, struct Lfm *lfm, const char *line) {
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
