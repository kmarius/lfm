#include "mode.h"
#include "cmdline.h"
#include "hashtab.h"
#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "trie.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

static void mode_free(void *p);

void normal_on_enter(Lfm *lfm);

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
  lfm->current_mode = ht_get(&lfm->modes, "normal");
  lfm->input_mode = ht_get(&lfm->modes, "input");
  lfm->maps.normal = lfm->current_mode->maps;
  lfm->maps.input = lfm->input_mode->maps;
}

void lfm_modes_deinit(Lfm *lfm) {
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
  ui_cmd_clear(&lfm->ui);
  cmdline_prefix_set(&lfm->ui.cmdline, "");
}

int lfm_mode_register(Lfm *lfm, const struct mode *mode) {
  if (ht_get(&lfm->modes, mode->name) != NULL) {
    lfm_error(lfm, "mode %s already exists", mode->name);
    return 1;
  }
  struct mode *newmode = calloc(1, sizeof *newmode);
  memcpy(newmode, mode, sizeof *newmode);
  newmode->name = strdup(mode->name);
  if (newmode->prefix) {
    newmode->prefix = strdup(newmode->prefix);
  }
  newmode->maps = trie_create();
  ht_set(&lfm->modes, newmode->name, newmode);
  return 0;
}

static inline void lfm_mode_transition_to(Lfm *lfm, struct mode *mode) {
  struct mode *current = lfm->current_mode;
  log_debug("mode transition from %s to %s", current->name, mode->name);
  mode_on_exit(current, lfm);
  lfm->current_mode = mode;
  mode_on_enter(mode, lfm);
  ui_redraw(&lfm->ui, REDRAW_INFO);
}

int lfm_mode_enter(Lfm *lfm, const char *name) {
  struct mode *mode = ht_get(&lfm->modes, name);
  if (!mode) {
    return 1;
  }
  lfm_mode_transition_to(lfm, mode);
  lfm->maps.cur_input = NULL;
  return 0;
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
