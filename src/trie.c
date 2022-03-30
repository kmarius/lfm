#include <stdio.h>
#include <stdlib.h>

#include "cvector.h"
#include "trie.h"

#define T Trie

static inline T *trie_node_create(input_t key, T *next)
{
	T *n = malloc(sizeof(*n));
	n->key = key;
	n->keys = NULL;
	n->desc = NULL;
	n->child = NULL;
	n->next = next;
	return n;
}


T *trie_create()
{
	return trie_node_create(0, NULL);
}


T *trie_find_child(const T* t, input_t key)
{
	if (!t)
		return NULL;

	for (T *n = t->child; n; n = n->next) {
		if (n->key == key)
			return n;
	}

	return NULL;
}


T *trie_insert(T* t, const input_t *trie_keys, const char *keys, const char *desc)
{
	if (!t)
		return NULL;

	for (const input_t *c = trie_keys; *c != 0; c++) {
		T *n = trie_find_child(t, *c);
		if (!n) {
			n = trie_node_create(*c, t->child);
			t->child = n;
		}
		t = n;
	}
	free(t->desc);
	free(t->keys);
	t->desc = desc ? strdup(desc) : NULL;
	t->keys = keys ? strdup(keys) : NULL;
	return t;
}


T *trie_remove(T* t, const input_t *trie_keys)
{
	if (*trie_keys == 0) {
		free(t->keys);
		free(t->desc);
		t->keys = NULL;
		t->desc = NULL;
		return t;
	}

	T **prev = &t->child;
	for (T *n = t->child; n; n = n->next) {
		if (n->key == *trie_keys) {
			T *ret = trie_remove(n, trie_keys + 1);
			if (!n->child && !n->keys) {
				*prev = n->next;
				free(n);
			}
			return ret;
		}
		prev = &n->next;
	}
	return NULL;
}


void trie_collect_leaves(Trie *t, cvector_vector_type(Trie *) *vec, bool prune)
{
	if (!t)
		return;

	if (t->keys) {
		cvector_push_back(*vec, t);
		if (prune)
			return;
	}

	for (T *n = t->child; n; n = n->next)
		trie_collect_leaves(n, vec, prune);
}


void trie_destroy(T *t)
{
	if (!t)
		return;

	for (T* next, *n = t->child; n; n = next) {
		next = n->next;
		trie_destroy(n);
	}

	free(t->desc);
	free(t->keys);
	free(t);
}
