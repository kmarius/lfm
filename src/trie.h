#pragma once

#include "cvector.h"
#include "keys.h"

/*
 * Keybinds get maped to lua functions. We use the (unique) address of the node
 * to look up lua functions hence there is no dedicated field. The existence of
 * trie_node.keys is the indicator that a command for the current key sequence
 * exists.
 */

struct Trie;

typedef struct Trie {
	input_t key;
	char *keys; /* the full string of keys so we can print the menu */
	char *desc; /* description of the command, can be NULL */
	struct Trie *child; /* can be NULL */
	struct Trie *next; /* next sibling, can be NULL */
} Trie;

/*
 * Allocate a new trie root.
 */
Trie *trie_create();

/*
 * Free all resources belonging to the trie.
 */
void trie_destroy(Trie *trie);

/*
 * Insert a new key sequence into the tree. keys should be the (printable) key
 * sequence, trie_keys is the sequence of keys converted to integers and desc
 * an optional description of the command. Returns the pointer of the final
 * node the command is inserted in.
 */
Trie *trie_insert(Trie* trie, const input_t *trie_keys, const char *keys, const char *desc);

/*
 * Finds the top level child belonging to key if it exists, NULL otherwise.
 */
Trie *trie_find_child(const Trie* trie, input_t key);

/*
 * Collect all reachable leaves and puts for each a string with the full
 * sequence and the command description into vec.
 */
void trie_collect_leaves(const Trie *trie, cvector_vector_type(char*) *vec);
