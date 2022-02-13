#include <stdio.h>
#include <stdlib.h>

#include "trie.h"
#include "util.h" // asprintf

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


/* we need the address of the vector because we might reallocate when pushing  */
void trie_collect_leaves(const T *t, cvector_vector_type(char*) *vec)
{
	for (T *n = t->child; n; n = n->next) {
		if (n->keys) {
			char *s;
			asprintf(&s, "%s\t%s", n->keys, n->desc ? n->desc : "");
			cvector_push_back(*vec, s);
		} else {
			trie_collect_leaves(n, vec);
		}
	}
}


void trie_destroy(T *t)
{
	if (!t)
		return;

	for (T* n = t->child; n; n = n->next)
		trie_destroy(n);

	free(t->desc);
	free(t->keys);
	free(t);
}


#undef T
