#pragma once

#include "cvector.h"
#include "keys.h"

// Keybinds get maped to lua functions. We use the (unique) address of the node
// to look up lua functions hence there is no dedicated field. The existence of
// trie_node.keys is the indicator that a command for the current key sequence
// exists.

typedef struct Trie {
	input_t key;
	char *keys;	         // full key sequence (used for menu), NULL for non-leaves
	char *desc;          // description of the command, can be NULL
	struct Trie *child;  // can be NULL
	struct Trie *next;   // next sibling, can be NULL
} Trie;

// Allocate a new trie root.
Trie *trie_create();

// Free all resources belonging to the trie.
void trie_destroy(Trie *trie);

// Insert a new key sequence into the tree. keys should be the (printable) key
// sequence, `trie\_keys` is the sequence of keys converted to `input_t` and
// `desc` an optional description of the command. Returns the pointer of the
// final node the word is ended in.
Trie *trie_insert(Trie* trie, const input_t *trie_keys, const char *keys, const char *desc);

// Remove a word from the trie and prunes empty branches. Returns the address
// of the invalidated node.
Trie *trie_remove(Trie* trie, const input_t *trie_keys);

// Finds the top level child belonging to key if it exists, NULL otherwise.
Trie *trie_find_child(const Trie* trie, input_t key);

// Collect leaves and in the vector. If `prune` is `true`, only
// reachable leaves are collected.
void trie_collect_leaves(Trie *trie, cvector_vector_type(Trie *) *vec, bool prune);
