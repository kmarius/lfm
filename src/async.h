#pragma once

#include "containers.h"
#include <ev.h>

#include <stdbool.h>

#include <pthread.h>

struct Dir;
struct Preview;

struct result_queue {
  struct result *head;
  struct result *tail;
  pthread_mutex_t mutex;
};

typedef struct Async {
  struct tpool *tpool;
  struct result_queue queue;
  ev_async result_watcher;
  int unpacker_ref; // ref to mpack.Unpacker instance
} Async;

void async_init(Async *async);

void async_deinit(Async *async);

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(Async *async, struct Dir *dir);

// Reloads `dir` from disk.
void async_dir_load(Async *async, struct Dir *dir, bool dircounts);

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(Async *async, struct Preview *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(Async *async, struct Preview *pv);

void async_chdir(Async *async, const char *path, bool hook);

void async_notify_add(Async *async, struct Dir *dir);

void async_notify_preview_add(Async *async, struct Dir *dir);

// Takes ownership of bytes, unless it fails. Returns -1 on failure,
// indicating that the mpack lua library wasn't found
int async_lua(Async *async, struct bytes *chunk, int ref);
