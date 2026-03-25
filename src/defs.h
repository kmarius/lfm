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
      Notify *: container_of((Notify *)ptr, Lfm, notify),                      \
      Loader *: container_of((Loader *)ptr, Lfm, loader),                      \
      default: NULL)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
