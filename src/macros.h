#pragma once

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
