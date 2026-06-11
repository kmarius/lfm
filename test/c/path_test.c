// Implementation includes for STC
#define i_implement
#include <stc/cstr.h>

// Mock getpwd_buf to use getenv("PWD")
#include "defs.h"
#include <stdlib.h>
isize getpwd_buf(char *buf, usize bufsz) {
  const char *pwd = getenv("PWD");
  if (!pwd)
    return -1;
  size_t len = strlen(pwd);
  if (len >= bufsz)
    return -1;
  memcpy(buf, pwd, len + 1);
  return (isize)len;
}

#include "path.c"
#include "unity.h"

#define PWD "/home/marius/some/sub/dir"
#define PARENT "/home/marius/some/sub"

void setUp(void) {
}
void tearDown(void) {
}

static void check_normalize(const char *input, const char *expected) {
  cstr p = path_normalize_cstr(zsview_from(input), PWD);
  TEST_ASSERT_EQUAL_STRING(expected, cstr_str(&p));
  cstr_drop(&p);
}

void test_tilde(void) {
  check_normalize("~/Sync", "/home/marius/Sync");
  check_normalize("~", "/home/marius");
  check_normalize("~/", "/home/marius");
}

void test_tilde_dotdot(void) {
  check_normalize("~/..", "/home");
  check_normalize("~/../", "/home");
  check_normalize("~/../../../../../..", "/");
}

void test_slashes(void) {
  check_normalize("~//", "/home/marius");
  check_normalize("/", "/");
  check_normalize("//", "/");
  check_normalize("///////", "/");
  check_normalize("~///////", "/home/marius");
}

void test_dots(void) {
  check_normalize("/./././././", "/");
  check_normalize("/../..", "/");
}

void test_relative(void) {
  check_normalize("./", PWD);
  check_normalize(".", PWD);
  check_normalize("..", PARENT);
  check_normalize("../", PARENT);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_tilde);
  RUN_TEST(test_tilde_dotdot);
  RUN_TEST(test_slashes);
  RUN_TEST(test_dots);
  RUN_TEST(test_relative);
  return UNITY_END();
}
