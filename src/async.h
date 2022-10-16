#pragma once

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct dir_s;
struct preview_s;

struct result_queue {
  struct result_s *head;
  struct result_s *tail;
  pthread_mutex_t mutex;
};

typedef struct async_s {
  struct lfm_s *lfm;
  struct tpool *tpool;
  struct result_queue queue;
  ev_async result_watcher;
} Async;

void async_init(Async *async, struct lfm_s *lfm);

void async_deinit(Async *async);

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(Async *async, struct dir_s *dir);

// Reloads `dir` from disk.
void async_dir_load(Async *async, struct dir_s *dir, bool dircounts);

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(Async *async, struct preview_s *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(Async *async, struct preview_s *pv);

void async_chdir(Async *async, const char *path);
