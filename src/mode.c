#include "mode.h"

#include "cmdline.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "lua/lfmlua.h"
#include "stc/cstr.h"
#include "trie.h"
#include "ui.h"

static void normal_on_enter(Lfm *lfm);

static void visual_on_enter(Lfm *lfm);
static void visual_on_exit(Lfm *lfm);

void lfm_modes_init(Lfm *lfm) {
  lfm->modes = hmap_modes_init();
  lfm_mode_register(lfm, &(struct mode){
                             .name = cstr_lit("normal"),
                             .on_enter = normal_on_enter,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = cstr_lit("input"),
                             .is_input = true,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = cstr_lit("visual"),
                             .is_input = false,
                             .on_enter = visual_on_enter,
                             .on_exit = visual_on_exit,
                         });
  const struct mode *input = hmap_modes_at(&lfm->modes, c_zv("input"));
  lfm->ui.maps.input = input->maps;
  lfm->current_mode = hmap_modes_at_mut(&lfm->modes, c_zv("normal"));
  lfm->ui.maps.normal = lfm->current_mode->maps;

  // TODO: should be done properly eventually, like we do with input modes
  struct mode *visual = hmap_modes_at_mut(&lfm->modes, c_zv("visual"));
  trie_destroy(visual->maps);
  visual->maps = lfm->ui.maps.normal;
}

void lfm_modes_deinit(Lfm *lfm) {
  // these maps belong to normal mode, don't double free
  hmap_modes_at_mut(&lfm->modes, c_zv("visual"))->maps = NULL;
  hmap_modes_drop(&lfm->modes);
}

static void normal_on_enter(Lfm *lfm) {
  cmdline_clear(&lfm->ui.cmdline);
}

static void visual_on_enter(Lfm *lfm) {
  fm_on_visual_enter(&lfm->fm);
  ui_redraw(&lfm->ui, REDRAW_FM);
}

static void visual_on_exit(Lfm *lfm) {
  fm_on_visual_exit(&lfm->fm);
}

int lfm_mode_register(Lfm *lfm, struct mode *mode) {
  if (hmap_modes_contains(&lfm->modes, cstr_zv(&mode->name))) {
    return 1;
  }
  // TODO: modes might change when if the table is resized, this dangerous, we
  // hand out references to lua
  cstr current = lfm->current_mode ? lfm->current_mode->name : cstr_init();
  hmap_modes_result res =
      hmap_modes_emplace(&lfm->modes, cstr_zv(&mode->name), *mode);
  res.ref->first = res.ref->second.name;
  if (!cstr_is_empty(&current)) {
    lfm->current_mode = hmap_modes_at_mut(&lfm->modes, cstr_zv(&current));
  }
  return 0;
}

int lfm_mode_enter(Lfm *lfm, zsview name) {
  hmap_modes_value *v = hmap_modes_get_mut(&lfm->modes, name);
  if (v == NULL)
    return 1;

  struct mode *mode = &v->second;
  if (mode == lfm->current_mode)
    return 0;

  mode_on_exit(lfm->current_mode, lfm);
  lfm->current_mode = mode;
  mode_on_enter(mode, lfm);

  if (mode->is_input && !cstr_is_empty(&mode->prefix)) {
    cmdline_prefix_set(&lfm->ui.cmdline, cstr_zv(&mode->prefix));
  }
  lfm->ui.maps.cur_input = NULL;
  lfm_run_hook(lfm, LFM_HOOK_MODECHANGED, &mode->name);

  ui_redraw(&lfm->ui, REDRAW_INFO | REDRAW_CMDLINE);
  return 0;
}

int lfm_mode_exit(Lfm *lfm, zsview name) {
  if (cstr_equals_zv(&lfm->current_mode->name, &name)) {
    return lfm_mode_normal(lfm);
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

void mode_on_return(struct mode *mode, struct Lfm *lfm, zsview line) {
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
