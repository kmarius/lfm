#pragma once

#include "stc/zsview.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Typedefs
 */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;
typedef ssize_t isize;

typedef float f32;
typedef double f64;
_Static_assert(sizeof(float) == 4, "float is not 4 bytes");
_Static_assert(sizeof(double) == 8, "double is not 8 bytes");

/*
 * Macros
 */

// pass no arguments to indicate everything nonnull
#define __lfm_nonnull(...) __attribute__((nonnull(__VA_ARGS__)))

#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })

#define to_lfm(ptr)                                                            \
  _Generic((ptr),                                                              \
      Ui *: container_of((Ui *)ptr, Lfm, ui),                                  \
      Fm *: container_of((Fm *)ptr, Lfm, fm),                                  \
      const Fm *: container_of((const Fm *)ptr, Lfm, fm),                      \
      Async *: container_of((Async *)ptr, Lfm, async),                         \
      struct inotify_ctx *: container_of((struct inotify_ctx *)ptr, Lfm,       \
                                         inotify),                             \
      Loader *: container_of((Loader *)ptr, Lfm, loader),                      \
      default: NULL)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Macros for variadic macros, modified to work with empty __VA_ARGS__

#define CONCATENATE(arg1, arg2) CONCATENATE1(arg1, arg2)
#define CONCATENATE1(arg1, arg2) CONCATENATE2(arg1, arg2)
#define CONCATENATE2(arg1, arg2) arg1##arg2

#define FOR_EACH_0(what, x)

#define FOR_EACH_1(what, x) what(x)

#define FOR_EACH_2(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_1(what, __VA_ARGS__);

#define FOR_EACH_3(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_2(what, __VA_ARGS__);

#define FOR_EACH_4(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_3(what, __VA_ARGS__);

#define FOR_EACH_5(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_4(what, __VA_ARGS__);

#define FOR_EACH_6(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_5(what, __VA_ARGS__);

#define FOR_EACH_7(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_6(what, __VA_ARGS__);

#define FOR_EACH_8(what, x, ...)                                               \
  what(x);                                                                     \
  FOR_EACH_7(what, __VA_ARGS__);

#define FOR_EACH_NARG(...) FOR_EACH_NARG_(_0, ##__VA_ARGS__, FOR_EACH_RSEQ_N())
#define FOR_EACH_NARG_(...) FOR_EACH_ARG_N(__VA_ARGS__)
#define FOR_EACH_ARG_N(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define FOR_EACH_RSEQ_N() 7, 6, 5, 4, 3, 2, 1, 0

#define FOR_EACH_(N, what, ...) CONCATENATE(FOR_EACH_, N)(what, __VA_ARGS__)
#define FOR_EACH(what, ...)                                                    \
  do {                                                                         \
    FOR_EACH_(FOR_EACH_NARG(__VA_ARGS__), what, __VA_ARGS__);                  \
  } while (0)

#define FOR_EACH1_0(what, a, x)

#define FOR_EACH1_1(what, a, x) what(a, x)

#define FOR_EACH1_2(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_1(what, a, __VA_ARGS__);

#define FOR_EACH1_3(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_2(what, a, __VA_ARGS__);

#define FOR_EACH1_4(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_3(what, a, __VA_ARGS__);

#define FOR_EACH1_5(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_4(what, a, __VA_ARGS__);

#define FOR_EACH1_6(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_5(what, a, __VA_ARGS__);

#define FOR_EACH1_7(what, a, x, ...)                                           \
  what(a, x);                                                                  \
  FOR_EACH1_6(what, a, __VA_ARGS__);

#define FOR_EACH1_(N, what, a, ...)                                            \
  CONCATENATE(FOR_EACH1_, N)(what, a, __VA_ARGS__)

// like FOR_EACH, but calls what with fixed first argument `a`
#define FOR_EACH1(what, a, ...)                                                \
  do {                                                                         \
    FOR_EACH1_(FOR_EACH_NARG(__VA_ARGS__), what, a, __VA_ARGS__);              \
  } while (0)
