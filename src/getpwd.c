#include "pwd.h"
#include "stc/cstr.h"

#include <pthread.h>
#include <stdio.h>

static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
static cstr PWD = cstr_init();

void setpwd(const char *path) {
  pthread_rwlock_wrlock(&lock);
  cstr_assign(&PWD, path);
  pthread_rwlock_unlock(&lock);
}

size_t getpwd_buf(char *buf, size_t bufzs) {
  pthread_rwlock_rdlock(&lock);
  ssize_t len = cstr_size(&PWD);
  if (len + 1 > (ssize_t)bufzs) {
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
