#include "getpwd.h"

#include <stc/cstr.h>

#include <pthread.h>

static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
static cstr PWD = cstr_init();

void setpwd(const char *path) {
  pthread_rwlock_wrlock(&lock);
  cstr_assign(&PWD, path);
  pthread_rwlock_unlock(&lock);
}

isize getpwd_buf(char *buf, usize bufzs) {
  pthread_rwlock_rdlock(&lock);
  isize len = cstr_size(&PWD);
  if (unlikely(len + 1 > (isize)bufzs)) {
    pthread_rwlock_unlock(&lock);
    return -1;
  }
  memcpy(buf, cstr_str(&PWD), len + 1); // includes nul
  pthread_rwlock_unlock(&lock);
  return len;
}

const char *getpwd_manual_unlock() {
  pthread_rwlock_rdlock(&lock);
  return cstr_str(&PWD);
}

zsview getpwd_zv_manual_unlock() {
  pthread_rwlock_rdlock(&lock);
  return cstr_zv(&PWD);
}

void getpwd_unlock() {
  pthread_rwlock_unlock(&lock);
}
