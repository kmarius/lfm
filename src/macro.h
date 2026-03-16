#pragma once

#include "keys.h"

#include <stdbool.h>
#include <stdint.h>

struct Lfm;

extern bool macro_recording;     // true, if currently recording a macro
extern bool macro_playing;       // true, if currently playing a macro
extern input_t macro_identifier; // identifier of the macro being recorded

void macros_init(void);
void macros_deinit(void);

// Begin recording the macro with the id `id`.
// Returns 0 on success, -1 if already recording a macro.
i32 macro_record(input_t id);

// Stop recording a macro.
// Returns 0 on success, -1 not currently recording.
i32 macro_stop_record(void);

// Play the macro with the id `id`.
// Returns 0 on success, -1 if no macro with the given id exists.
i32 macro_play(input_t id, struct Lfm *lfm);

// Add an input to the macro currently being recorded.
// Returns 0 on success, -1 if not recording.
i32 macro_add_key(input_t key);
