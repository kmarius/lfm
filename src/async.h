#pragma once

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "dir.h"
#include "preview.h"
#include "tpool.h"

typedef struct Result Result;

typedef struct ResultQueue {
	Result *head;
	Result *tail;
	pthread_mutex_t mutex;
	ev_async *watcher;
} ResultQueue;

struct async_watcher_data {
	App *app;
	ResultQueue *queue;
};

void async_init(App *app);

void async_deinit();

// Returns a `res_t` from `queue` if it is non-empty, `NULL` otherwise.
Result *resultqueue_get(ResultQueue *queue);

// Destroy all results that remain in the queue.
void resultqueue_deinit(ResultQueue *queue);

// Process the result and free its resources.
void result_process(Result *res, App *app);

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(Dir *dir);

// Reloads `dir` from disk after `delay` milliseconds. Negative delays are ignored.
void async_dir_load_delayed(Dir *dir, bool dircounts, uint16_t delay);

// Loads `dir` from disk.
#define async_dir_load(dir, dircounts) async_dir_load_delayed(dir, dircounts, 0)

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(Preview *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(Preview *pv, uint16_t nrow);
