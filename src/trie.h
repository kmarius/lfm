#ifndef TRIE_H
#define TRIE_H

#include "cvector.h"

/*
 * Keybinds get maped to lua functions. We use the (unique) address of the node
 * to look up lua functions hence there is no dedicated field. The existence of
 * trie_node.keys is the indicator that a command for the current key sequence
 * exists.
 */
typedef struct trie_node_t {
	int key;
	char *keys; /* the full string of keys so we can print the menu */
	char *desc; /* description of the command, can be NULL */
	struct trie_node_t *child; /* can be NULL */
	struct trie_node_t *next; /* next sibling, can be NULL */
} trie_node_t;

/*
 * Allocate a new trie root
 */
trie_node_t *trie_new();

/*
 * Insert a new key sequence into the tree. keys should be the (printable) key
 * sequence, trie_keys is the sequence of keys converted to integers and desc
 * an optional description of the command. Returns the pointer of the final
 * node the command is inserted in.
 */
trie_node_t *trie_insert(trie_node_t* trie, const int *trie_keys, const char *keys, const char *desc);

/*
 * Finds the top level child belonging to key if it exists, NULL otherwise.
 */
trie_node_t *trie_find_child(const trie_node_t* trie, int key);

/*
 * Collect all reachable leaves and puts for each a string with the full
 * sequence and the command description into vec.
 */
void trie_collect_leaves(const trie_node_t *trie, cvector_vector_type(char*) *vec);

/*
 * Free all resources belonging to the trie.
 */
void trie_destroy(trie_node_t *trie);

#endif /* TRIE_H */
