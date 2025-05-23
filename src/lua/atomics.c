#include <stdatomic.h>
#include <stdint.h>

// to use from lua via ffi

int atomic_fetch_sub_(_Atomic int *atomic, int val) {
  return atomic_fetch_sub(atomic, val);
}

int atomic_fetch_add_(_Atomic int *atomic, int val) {
  return atomic_fetch_add(atomic, val);
}
