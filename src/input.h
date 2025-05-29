#pragma once

#include "keys.h"
#include "stc/zsview.h"
#include "trie.h"

struct Lfm;

// Initialization needs to happen after notcurses is running.
__lfm_nonnull()
void input_init(struct Lfm *lfm);

__lfm_nonnull()
void input_deinit(struct Lfm *lfm);

// Needs to be called when notcurses is restarted, because inputready_fd
// changes.
__lfm_nonnull()
void input_resume(struct Lfm *lfm);

// Stop listening to input:
__lfm_nonnull()
void input_suspend(struct Lfm *lfm);

__lfm_nonnull()
void input_handle_key(struct Lfm *lfm, input_t in);

// Maps a key sequence to a lua function (i.e. a reference to the registry).
// `ref == 0` unmaps. Returns -1 on invalid key sequence, -2 on input too long.
// removed.
__lfm_nonnull()
int input_map(Trie *trie, zsview keys, int ref, zsview desc, int *out_ref);

// Unmap a key sequence.
__lfm_nonnull()
static inline int input_unmap(Trie *trie, zsview keys, int *out_ref) {
  return input_map(trie, keys, 0, c_zv(""), out_ref);
}
