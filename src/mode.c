#include "mode.h"

#include "cmdline.h"
#include "hooks.h"
#include "lfm.h"
#include "lua/lfmlua.h"
#include "stc/cstr.h"
#include "trie.h"
#include "ui.h"
#include "visual.h"

static void normal_on_enter(Lfm *lfm);

void lfm_modes_init(Lfm *lfm) {
  lfm->modes = hmap_modes_init();
  lfm_mode_register(lfm, &(struct mode){
                             .name = cstr_lit("normal"),
                             .type = MODE_BUILTIN,
                             .on_enter = normal_on_enter,
                         });
  lfm_mode_register(lfm, &(struct mode){
                             .name = cstr_lit("input"),
                             .type = MODE_BUILTIN,
                             .is_input = true,
                         });
  lfm_mode_register(lfm, &visual_mode);

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

bool lfm_mode_exists(Lfm *lfm, zsview name) {
  return hmap_modes_contains(&lfm->modes, name);
}

static void normal_on_enter(Lfm *lfm) {
  cmdline_clear(&lfm->ui.cmdline);
}

i32 lfm_mode_register(Lfm *lfm, struct mode *mode) {
  if (lfm_mode_exists(lfm, cstr_zv(&mode->name))) {
    return 1;
  }
  cstr current = lfm->current_mode ? lfm->current_mode->name : cstr_init();
  hmap_modes_result res =
      hmap_modes_emplace(&lfm->modes, cstr_zv(&mode->name), *mode);
  res.ref->first = res.ref->second.name;
  // reassign current mode, table might have been resized
  if (!cstr_is_empty(&current)) {
    lfm->current_mode = hmap_modes_at_mut(&lfm->modes, cstr_zv(&current));
  }
  return 0;
}

i32 lfm_mode_enter(Lfm *lfm, zsview name) {
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
  LFM_RUN_HOOK(lfm, LFM_HOOK_MODECHANGED, &mode->name);

  ui_redraw(&lfm->ui, REDRAW_INFO | REDRAW_CMDLINE);
  return 0;
}

i32 lfm_mode_exit(Lfm *lfm, zsview name) {
  if (cstr_equals_zv(&lfm->current_mode->name, name)) {
    return lfm_mode_normal(lfm);
  }
  return 1;
}

void mode_on_enter(struct mode *mode, Lfm *lfm) {
  if (mode->type == MODE_BUILTIN) {
    if (mode->on_enter)
      mode->on_enter(lfm);
  } else if (mode->on_enter_ref) {
    lfm_lua_cb(lfm->L, mode->on_enter_ref, false);
  }
}

void mode_on_return(struct mode *mode, struct Lfm *lfm, zsview line) {
  if (mode->type == MODE_BUILTIN) {
    if (mode->on_return)
      mode->on_return(lfm, line);
  } else if (mode->on_return_ref) {
    lfm_lua_cb1(lfm->L, mode->on_return_ref, line);
  }
}

void mode_on_change(struct mode *mode, Lfm *lfm) {
  if (mode->type == MODE_BUILTIN) {
    if (mode->on_change)
      mode->on_change(lfm);
  } else if (mode->on_change_ref) {
    lfm_lua_cb(lfm->L, mode->on_change_ref, false);
  }
}

void mode_on_esc(struct mode *mode, Lfm *lfm) {
  if (mode->type == MODE_BUILTIN) {
    if (mode->on_esc)
      mode->on_esc(lfm);
  } else if (mode->on_esc_ref) {
    lfm_lua_cb(lfm->L, mode->on_esc_ref, false);
  }
}

void mode_on_exit(struct mode *mode, Lfm *lfm) {
  if (mode->type == MODE_BUILTIN) {
    if (mode->on_exit)
      mode->on_exit(lfm);
  } else if (mode->on_exit_ref) {
    lfm_lua_cb(lfm->L, mode->on_exit_ref, false);
  }
}
