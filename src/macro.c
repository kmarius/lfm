#include "macro.h"

#include "input.h"
#include "keys.h"
#include "lfm.h"
#include "log.h"

#include <stdint.h>

#define i_type macros_map, input_t, vec_input
#include "stc/hmap.h"

bool macro_recording = false;
bool macro_playing = false;
input_t macro_identifier;

static macros_map macros = {0};
static struct vec_input *current = NULL;

void macros_init() {
}

void macros_deinit() {
  macros_map_drop(&macros);
}

int macro_record(uint64_t id) {
  if (macro_recording) {
    log_error("already recording a macro");
    return -1;
  }
  if (macro_playing) {
    log_error("currently playing a macro");
    return -1;
  }
  macros_map_value *v = macros_map_get_mut(&macros, id);
  if (v == NULL) {
    macros_map_result res = macros_map_insert(&macros, id, vec_input_init());
    current = &res.ref->second;
  } else {
    current = &v->second;
  }
  macro_recording = true;
  macro_identifier = id;
  return 0;
}

int macro_stop_record() {
  if (!macro_recording) {
    log_error("tried to stop recording while not recording");
    return -1;
  }
  // Assuming the last key called this function
  current->size--;
  current = NULL;
  macro_recording = false;
  return 0;
}

int macro_play(uint64_t id, struct Lfm *lfm) {
  if (macro_recording) {
    log_error("can not play macro while recording");
    return -1;
  }
  if (macro_playing) {
    log_error("a macro is already playing");
    return -1;
  }
  macros_map_value *v = macros_map_get_mut(&macros, id);
  if (v != NULL) {
    macro_playing = true;
    c_foreach(it, vec_input, v->second) {
      input_handle_key(lfm, *it.ref);
    }
    macro_playing = false;
    return 0;
  }
  log_error("macro not found");
  return -1;
}

int macro_add_key(input_t key) {
  if (current == NULL) {
    log_error("macro_add_key called but not recording");
    return -1;
  }
  vec_input_push_back(current, key);
  return 0;
}
