#include "macros.h"

#include "input.h"
#include "keys.h"
#include "lfm.h"
#include "log.h"

#include <stdint.h>

struct macro {
  input_t *inputs;
  int length;
  int capacity;
};

struct {
  struct macro macro;
  input_t id;
} *macros;
static int length = 0;
static int capacity = 8;

bool macro_recording = false;
bool macro_playing = false;
input_t macro_identifier;

static struct macro *current = NULL;

void macros_init() {
  macros = malloc(capacity * sizeof *macros);
}

void macros_deinit() {
  for (int i = 0; i < length; i++) {
    free(macros[i].macro.inputs);
  }
  free(macros);
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
  for (int i = 0; i < length; i++) {
    if (macros[i].id == id) {
      current = &macros[i].macro;
      break;
    }
  }
  if (!current) {
    if (length == capacity) {
      capacity *= 2;
      macros = realloc(macros, capacity * sizeof *macros);
    }
    macros[length].id = id;
    current = &macros[length].macro;
    length++;
    current->capacity = 8;
    current->inputs = malloc(current->capacity * sizeof *current->inputs);
  }
  current->length = 0;
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
  current->length--;
  current = NULL;
  macro_recording = false;
  return 0;
}

static inline int macro_play_impl(Lfm *lfm, struct macro *macro) {
  for (int j = 0; j < macro->length; j++) {
    log_trace("%s", input_to_key_name(macro->inputs[j]));
  }
  for (int j = 0; j < macro->length; j++) {
    input_handle_key(lfm, macro->inputs[j]);
  }
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
  for (int i = 0; i < length; i++) {
    if (macros[i].id == id) {
      macro_playing = true;
      int ret = macro_play_impl(lfm, &macros[i].macro);
      macro_playing = false;
      return ret;
    }
  }
  log_error("macro not found");
  return -1;
}

static inline int macro_add_key_impl(struct macro *macro, input_t key) {
  if (macro->length == macro->capacity) {
    macro->capacity *= 2;
    macro->inputs =
        realloc(macro->inputs, macro->capacity * sizeof *macro->inputs);
  }
  macro->inputs[macro->length++] = key;
  return 0;
}

int macro_add_key(input_t key) {
  if (!current) {
    log_error("macro_add_key called but not recording");
    return -1;
  }
  return macro_add_key_impl(current, key);
}
