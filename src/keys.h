#pragma once

#include "defs.h"

#include <notcurses/notcurses.h>
#include <stc/zsview.h>

// if the highest 3 bits are used in notcurses at some point we need to
// move up to u64
typedef u32 input_t;

#define SHIFT_MASK ((input_t)1 << (sizeof(input_t) * 8 - 1))
#define CTRL_MASK ((input_t)1 << (sizeof(input_t) * 8 - 2))
#define ALT_MASK ((input_t)1 << (sizeof(input_t) * 8 - 3))

// key without modifiers
#define ID(u) ((input_t)(u & 0x1fffffff))

// test modifier
#define ISSHIFT(u) !!((u) & SHIFT_MASK)
#define ISCTRL(u) !!((u) & CTRL_MASK)
#define ISALT(u) !!((u) & ALT_MASK)
#define ISMODIFIED(u) !!((u) & (SHIFT_MASK | CTRL_MASK | ALT_MASK))

// add modifier
#define SHIFT(u) ((u) | SHIFT_MASK)
#define CTRL(u) ((u) | CTRL_MASK)
#define ALT(u) ((u) | ALT_MASK)

// ncinput_alt_p doesn't seem to work correctly, use deprecated alt field
#define ncinput_alt_p(in) !!((in)->alt)

// Convert an ncinput to ncinput_t. We use the three most significant bits to
// store modifier keys.
#define ncinput_to_input(in)                                                   \
  (input_t)((in)->id | (ncinput_alt_p(in) ? ALT_MASK : 0) |                    \
            (ncinput_ctrl_p(in) ? CTRL_MASK : 0) |                             \
            (ncinput_shift_p(in) ? SHIFT_MASK : 0))

// Map an `input_t` to a statically allocated string containing its readable
// representation. Not thread safe.
const char *input_to_key_name(input_t in, usize *len_out);

// returns -1 on error, length of the used chars otherwise
i32 key_name_to_input(const char *key, input_t *out);

// Map a string of inputs in its readable representation to a zero terminated
// array of `input_t`s in the given buffer. `buf` should be of the size at
// least `strlen(keys)+1`. Returns -1 on error.
i32 key_names_to_input(zsview keys, input_t *buf, usize bufsz);
