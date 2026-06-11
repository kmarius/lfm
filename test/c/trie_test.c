#define i_implement
#include <stc/cstr.h>

#include "trie.c"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

// Helper to find a key sequence in the trie, returns the ref or 0
static i32 trie_find(Trie *t, const input_t *keys) {
  for (const input_t *c = keys; *c != 0; c++) {
    t = trie_find_child(t, *c);
    if (!t)
      return 0;
  }
  return t->ref;
}

void test_create_destroy(void) {
  Trie *t = trie_create();
  TEST_ASSERT_NOT_NULL(t);
  trie_destroy(t);
}

void test_insert_find(void) {
  Trie *t = trie_create();

  // Insert "ab" with ref 1
  input_t ab[] = {'a', 'b', 0};
  trie_insert(t, ab, 1, c_zv("ab"), c_zv(""));
  TEST_ASSERT_EQUAL(1, trie_find(t, ab));

  // Insert "abc" with ref 2
  input_t abc[] = {'a', 'b', 'c', 0};
  trie_insert(t, abc, 2, c_zv("abc"), c_zv(""));
  TEST_ASSERT_EQUAL(2, trie_find(t, abc));
  TEST_ASSERT_EQUAL(1, trie_find(t, ab));  // ab still exists

  // Insert "abd" with ref 3
  input_t abd[] = {'a', 'b', 'd', 0};
  trie_insert(t, abd, 3, c_zv("abd"), c_zv(""));
  TEST_ASSERT_EQUAL(3, trie_find(t, abd));

  // "abcd" doesn't exist
  input_t abcd[] = {'a', 'b', 'c', 'd', 0};
  TEST_ASSERT_EQUAL(0, trie_find(t, abcd));

  trie_destroy(t);
}

void test_remove(void) {
  Trie *t = trie_create();

  input_t ab[] = {'a', 'b', 0};
  input_t abc[] = {'a', 'b', 'c', 0};

  trie_insert(t, ab, 1, c_zv("ab"), c_zv(""));
  trie_insert(t, abc, 2, c_zv("abc"), c_zv(""));

  // Remove abc
  i32 ref = trie_remove(t, abc);
  TEST_ASSERT_EQUAL(2, ref);
  TEST_ASSERT_EQUAL(0, trie_find(t, abc));
  TEST_ASSERT_EQUAL(1, trie_find(t, ab));  // ab still exists

  trie_destroy(t);
}

void test_replace(void) {
  Trie *t = trie_create();

  input_t ab[] = {'a', 'b', 0};
  trie_insert(t, ab, 1, c_zv("ab"), c_zv(""));
  TEST_ASSERT_EQUAL(1, trie_find(t, ab));

  // Replace with new ref
  i32 old = trie_insert(t, ab, 5, c_zv("ab"), c_zv(""));
  TEST_ASSERT_EQUAL(1, old);
  TEST_ASSERT_EQUAL(5, trie_find(t, ab));

  trie_destroy(t);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_create_destroy);
  RUN_TEST(test_insert_find);
  RUN_TEST(test_remove);
  RUN_TEST(test_replace);
  return UNITY_END();
}
