#include "keys.h"
#include "trie.h"

struct lfm_s;

void input_init(struct lfm_s *lfm);
void input_deinit(struct lfm_s *lfm);

void lfm_handle_key(struct lfm_s *lfm, input_t in);

// Maps a key sequence to a lua function (i.e. a reference to the registry).
// Pass `ref == 0` to unmap. Returns the previous reference/reference that was removed.
static inline int input_map(Trie *trie, const char *keys, int ref, const char *desc)
{
  input_t *buf = malloc((strlen(keys) + 1) * sizeof *buf);
  key_names_to_input(keys, buf);
  int ret = ref ?
    trie_insert(trie, buf, ref, keys, desc)
    : trie_remove(trie, buf);
  free(buf);
  return ret;
}

static inline int input_unmap(Trie *trie, const char *keys)
{
  return input_map(trie, keys, 0, NULL);
}
