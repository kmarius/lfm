#pragma once

#include "keys.h"
#include "util.h" // strcasecmp_strict

#include <stc/cstr.h>

#include <strings.h>

#include <sys/cdefs.h>

// Stores key-values of input_t* -> (i32, keys, desc). Can't store 0 because it
// signals that a node is empty.

typedef struct Trie {
  input_t key;
  struct Trie *child; // can be NULL
  struct Trie *next;  // next sibling, can be NULL
  struct {
    bool is_leaf; // true for mapped key sequence
    i32 ref;      // reference to a function in the registry, or 0
    cstr keys;    // full key sequence (used for menu), empty for non-leaves
    cstr desc;    // description of the command
  };
} Trie;

// we can sort the vector before showing suggestions on the ui
#define i_type vec_trie, Trie *
#define i_cmp(l, r)                                                            \
  (strcasecmp_strict(cstr_str(&(*l)->keys), cstr_str(&(*r)->keys)))
#include <stc/vec.h>

// Allocate a new trie root.
Trie *trie_create(void);

// Free all resources belonging to the trie.
void trie_destroy(Trie *trie);

// Insert a new key/val tuple into the tree. `keys` should be the (printable)
// key sequence, `desc` an optional description of the command.
// Returns the value that was replaced (or 0 if none was).
__lfm_nonnull(1, 2)
i32 trie_insert(Trie *trie, const input_t *trie_keys, i32 ref, zsview keys,
                zsview desc);

// Remove a key/val from the trie and returns the value.
__lfm_nonnull(1, 2)
i32 trie_remove(Trie *trie, const input_t *trie_keys);

// Finds the top level child belonging to key if it exists, NULL otherwise.
__lfm_nonnull(1)
Trie *trie_find_child(Trie *trie, input_t key);

// Collect leaves and in the vector. If `prune` is `true`, only
// reachable leaves are collected.
__lfm_nonnull(1)
vec_trie trie_collect_leaves(Trie *trie, bool prune);
