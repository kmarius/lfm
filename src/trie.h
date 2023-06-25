#pragma once

#include "cvector.h"
#include "keys.h"

// Stores key-values of input_t* -> int. Can't store 0 because it signals that a
// node is empty.

typedef struct trie_s {
  input_t key;
  struct trie_s *child;  // can be NULL
  struct trie_s *next;   // next sibling, can be NULL
  struct {
    int ref;             // reference to a function in the registry, or 0
    char *keys;          // full key sequence (used for menu), NULL for non-leaves
    char *desc;          // description of the command, can be NULL
  };
} Trie;

// Allocate a new trie root.
Trie *trie_create(void);

// Free all resources belonging to the trie.
void trie_destroy(Trie *trie);

// Insert a new key/val tuple into the tree. `keys` should be the (printable)
// key sequence, `desc` an optional description of the command.
// Returns the value that was replaced (or 0 if none was).
int trie_insert(Trie* trie, const input_t *trie_keys, int ref, const char *keys, const char *desc);

// Remove a key/val from the trie and returns the value.
int trie_remove(Trie* trie, const input_t *trie_keys);

// Finds the top level child belonging to key if it exists, NULL otherwise.
Trie *trie_find_child(const Trie* trie, input_t key);

// Collect leaves and in the vector. If `prune` is `true`, only
// reachable leaves are collected.
void trie_collect_leaves(Trie *trie, cvector_vector_type(Trie *) *vec, bool prune);
