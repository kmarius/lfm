#include "keys.h"
#include "trie.h"

struct Lfm;

// Initialization needs to happen after notcurses is running.
void input_init(struct Lfm *lfm);
void input_deinit(struct Lfm *lfm);

// Needs to be called when notcurses is restarted, because inputready_fd
// changes.
void input_resume(struct Lfm *lfm);

// Stop listening to input:
void input_suspend(struct Lfm *lfm);

void input_handle_key(struct Lfm *lfm, input_t in);

// Set input timout. Key input will be ignored for the next `duration` ms.
void input_timeout_set(struct Lfm *lfm, uint32_t duration);

// Maps a key sequence to a lua function (i.e. a reference to the registry).
// Pass `ref == 0` to unmap. Returns the previous reference/reference that was
// removed.
int input_map(Trie *trie, const char *keys, int ref, const char *desc);

// Unmap a key sequence.
static inline int input_unmap(Trie *trie, const char *keys) {
  return input_map(trie, keys, 0, NULL);
}
