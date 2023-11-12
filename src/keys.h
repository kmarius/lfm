#pragma once

#include <notcurses/notcurses.h>

// if the highest 3 bits are used in notcurses at some point we need to go
// move to uint64_t
typedef uint32_t input_t;

#define SHIFT_MASK ((input_t)1 << (sizeof(input_t) * 8 - 1))
#define CTRL_MASK ((input_t)1 << (sizeof(input_t) * 8 - 2))
#define ALT_MASK ((input_t)1 << (sizeof(input_t) * 8 - 3))
#define ID(u) ((input_t)(u & 0x1fffffff))
#define ISSHIFT(u) (u & SHIFT_MASK)
#define ISCTRL(u) (u & CTRL_MASK)
#define ISALT(u) (u & ALT_MASK)
#define SHIFT(u) (u | SHIFT_MASK)
#define CTRL(u) (u | CTRL_MASK)
#define ALT(u) (u | ALT_MASK)

// Convert an ncinput to ncinput_t. We use the three most significant bits to
// store modifier keys.
// ncinput_alt_p doesn't seem to work yet
#define ncinput_to_input(in)                                                   \
  (input_t)((in)->id | ((in)->alt ? ALT_MASK : 0) |                            \
            (ncinput_ctrl_p(in) ? CTRL_MASK : 0) |                             \
            (ncinput_shift_p(in) ? SHIFT_MASK : 0))

// Map an `input_t` to a statically allocated string containing its readable
// representation. Not thread safe.
const char *input_to_key_name(input_t in);

// Map a string of inputs in its readable representation to a zero terminated
// array of `input_t`s in the given buffer. `buf` should be of the size at
// least `strlen(keys)+1`.
input_t *key_names_to_input(const char *keys, input_t *buf);
