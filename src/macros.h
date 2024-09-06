#pragma once

#include "keys.h"

#include <stdbool.h>
#include <stdint.h>

struct Lfm;

extern bool macro_recording; // true, if currently recording a macro
extern bool macro_playing;   // true, if currently playing a macro

void macros_init(void);
void macros_deinit(void);

// Begin recording the macro with the id `id`.
// Returns 0 on success, -1 if already recording a macro.
int macro_record(uint64_t id);

// Stop recording a macro.
// Returns 0 on success, -1 not currently recording.
int macro_stop_record(void);

// Play the macro with the id `id`.
// Returns 0 on success, -1 if no macro with the given id exists.
int macro_play(uint64_t id, struct Lfm *lfm);

// Add an input to the macro currently being recorded.
// Returns 0 on success, -1 if not recording.
int macro_add_key(input_t key);
