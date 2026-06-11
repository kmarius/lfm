#include "tokenize.c"
#include "unity.h"
#include <stdarg.h>

void setUp(void) {}
void tearDown(void) {}

static void check_tokenize(const char *string, int count, ...) {
  char buf[128], *s;
  va_list sp;
  va_start(sp, count);
  const char *i;
  char *j;

  s = tokenize(string, buf, &i, &j);
  for (int k = 0; k < count; k++) {
    const char *expected = va_arg(sp, char *);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING(expected, s);
    s = tokenize(0, 0, &i, &j);
  }
  TEST_ASSERT_NULL(tokenize(0, 0, &i, &j));
  va_end(sp);
}

void test_simple(void) {
  check_tokenize("abc", 1, "abc");
  check_tokenize("abc   ", 1, "abc");
  check_tokenize("   abc", 1, "abc");
  check_tokenize("   abc   ", 1, "abc");
}

void test_multiple(void) {
  check_tokenize("abc def", 2, "abc", "def");
  check_tokenize("abc           def", 2, "abc", "def");
}

void test_quoted(void) {
  check_tokenize("\"abc\"", 1, "abc");
  check_tokenize("\"abc def\"", 1, "abc def");
  check_tokenize("        \"abc def\"          \"ghi jkl\"     ", 2, "abc def", "ghi jkl");
}

void test_escaped(void) {
  check_tokenize("abc\\ def", 1, "abc def");
  check_tokenize("abc\\ ", 1, "abc ");
  check_tokenize("abc\\u  yo", 2, "abc\\u", "yo");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_simple);
  RUN_TEST(test_multiple);
  RUN_TEST(test_quoted);
  RUN_TEST(test_escaped);
  return UNITY_END();
}
