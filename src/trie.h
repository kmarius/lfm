#pragma once

#include "keys.h"

// Stores key-values of input_t* -> int. Can't store 0 because it signals that a
// node is empty.

typedef struct Trie {
  input_t key;
  struct Trie *child; // can be NULL
  struct Trie *next;  // next sibling, can be NULL
  struct {
    int ref;    // reference to a function in the registry, or 0
    char *keys; // full key sequence (used for menu), NULL for non-leaves
    char *desc; // description of the command, can be NULL
  };
} Trie;

#define i_type vec_trie
#define i_val Trie *
#include "stc/vec.h"

// Allocate a new trie root.
Trie *trie_create(void);

// Free all resources belonging to the trie.
void trie_destroy(Trie *trie);

// Insert a new key/val tuple into the tree. `keys` should be the (printable)
// key sequence, `desc` an optional description of the command.
// Returns the value that was replaced (or 0 if none was).
int trie_insert(Trie *trie, const input_t *trie_keys, int ref, const char *keys,
                const char *desc);

// Remove a key/val from the trie and returns the value.
int trie_remove(Trie *trie, const input_t *trie_keys);

// Finds the top level child belonging to key if it exists, NULL otherwise.
Trie *trie_find_child(const Trie *trie, input_t key);

// Collect leaves and in the vector. If `prune` is `true`, only
// reachable leaves are collected.
vec_trie trie_collect_leaves(Trie *trie, bool prune);
