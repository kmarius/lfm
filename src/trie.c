#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "cvector.h"
#include "trie.h"

static trie_node_t *trie_node_new(long key, trie_node_t *next)
{
	trie_node_t *n = malloc(sizeof(*n));
	n->key = key;
	n->keys = NULL;
	n->desc = NULL;
	n->child = NULL;
	n->next = next;
	return n;
}

trie_node_t *trie_new()
{
	return trie_node_new(0, NULL);
}

trie_node_t *trie_find_child(const trie_node_t* trie, long key)
{
	if (!trie) {
		return NULL;
	}
	for (trie_node_t *n = trie->child; n; n = n->next) {
		if (n->key == key) {
			return n;
		}
	}
	return NULL;
}

trie_node_t *trie_insert(trie_node_t* trie, const long *trie_keys, const char *keys, const char *desc)
{
	trie_node_t *n;
	if (!trie) {
		return NULL;
	}
	for (const long *c = trie_keys; *c; c++) {
		n = trie_find_child(trie, *c);
		if (!n) {
			n = trie_node_new(*c, trie->child);
			trie->child = n;
		}
		trie = n;
	}
	trie->desc = desc ? strdup(desc) : NULL;
	trie->keys = strdup(keys);
	return trie;
}

void trie_collect_leaves(const trie_node_t *trie, cvector_vector_type(char*) *vec)
{
	for (trie_node_t *n = trie->child; n; n = n->next) {
		if (n->keys) {
			char *s;
			asprintf(&s, "%s\t%s", n->keys, n->desc ? n->desc : "");
			cvector_push_back(*vec, s);
		} else {
			trie_collect_leaves(n, vec);
		}
	}
}

void trie_destroy(trie_node_t *trie)
{
	if (!trie) {
		return;
	}
	for (trie_node_t* n = trie->child; n; n = n->next) {
		trie_destroy(n);
	}
	free(trie->desc);
	free(trie->keys);
	free(trie);
}
