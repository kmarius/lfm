#pragma once

#include <notcurses/notcurses.h>

typedef uint32_t input_t;

// if these bits are used we need to go higher
#define SHIFT_MASK ((input_t) 1 << 31)
#define CTRL_MASK ((input_t) 1 << 30)
#define ALT_MASK ((input_t) 1 << 29)
#define ID(u) ((input_t) (u & 0x1fffffff))
#define ISSHIFT(u) ((u&SHIFT_MASK) >> 31)
#define ISCTRL(u) ((u&CTRL_MASK) >> 30)
#define ISALT(u) ((u&ALT_MASK) >> 29)
#define SHIFT(u) (u|SHIFT_MASK)
#define CTRL(u) (u|CTRL_MASK)
#define ALT(u) (u|ALT_MASK)

// Convert an ncinput to ncinput_t. We use the three most significant bits to
// store modifier keys.
#define ncinput_to_input(in) \
  (input_t) ((in)->id \
      | ((in)->alt ? ALT_MASK : 0) \
      | ((in)->ctrl ? CTRL_MASK : 0) \
      | ((in)->shift ? SHIFT_MASK : 0))

// Map an `input_t` to a statically allocated string containing its readable
// representation. Not thread safe.
const char *input_to_key_name(input_t in);

// Map a string of inputs in its readable representation to a zero terminated
// array of `input_t`s in the given buffer. `buf` should be of the size at
// least `strlen(keys)+1`.
input_t *key_names_to_input(const char *keys, input_t *buf);
