#pragma once

#include "keys.h"
#include "trie.h"

#include <ev.h>
#include <stc/zsview.h>

#define i_type vec_input, input_t
#include <stc/vec.h>

#define i_type queue_input, input_t
#include <stc/queue.h>

struct input_state {
  Trie *cur;         // current leaf in the trie of the active mode
  Trie *cur_input;   // current leaf in the trie of input maps
  vec_input seq;     // current key sequence
  i32 count;         // count accumulator
  bool accept_count; // are we still accepting numbers as count
  Trie *normal_maps; // "normal" mode mappings
  Trie *input_maps;  // "input" mode mappings
  queue_input input_buffer;
  ev_io input_watcher;          // watch for input on notcurses' inputready fd
  ev_idle input_buffer_watcher; // processes keys in the input buffer once the
                                // event loops goes idle
  ev_timer map_clear_timer; // clear the currently buffered key sequence after a
                            // timeout
  ev_timer map_suggestion_timer; // show map suggestions after a timeout
};

struct Lfm;
struct notcurses;

__lfm_nonnull()
void input_init(struct input_state *state, struct Lfm *lfm);

__lfm_nonnull()
void input_deinit(struct input_state *state);

// Resume waiting input, after (re-)init of notcurses.
__lfm_nonnull()
void input_resume(struct input_state *state, struct notcurses *nc);

// Stop waiting for input.
__lfm_nonnull()
void input_suspend(struct input_state *state);

__lfm_nonnull()
void input_handle_key(struct input_state *state, struct Lfm *lfm, input_t in);

__lfm_nonnull()
void input_buffer_add(struct input_state *state, input_t in);

// Maps a key sequence to a lua function (i.e. a reference to the registry).
// `ref == 0` unmaps. Returns -1 on invalid key sequence, -2 on input too long.
// removed.
__lfm_nonnull()
i32 input_map(Trie *trie, zsview keys, i32 ref, zsview desc, i32 *out_ref);

// Unmap a key sequence.
__lfm_nonnull()
static inline i32 input_unmap(Trie *trie, zsview keys, i32 *out_ref) {
  return input_map(trie, keys, 0, c_zv(""), out_ref);
}
