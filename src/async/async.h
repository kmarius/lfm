#pragma once

#include <ev.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct Dir;
struct Preview;
struct Lfm;
struct bytes;

struct result_queue {
  struct result *head;
  struct result *tail;
  pthread_mutex_t mutex;
};

typedef struct Async {
  struct tpool *tpool;
  struct result_queue queue;
  ev_async result_watcher;
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

// Takes ownership of chunk and arg
void async_lua(struct Async *async, struct bytes *chunk, struct bytes *arg,
               int ref);

void async_lua_preview(struct Async *async, struct Preview *pv);
