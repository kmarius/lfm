#pragma once

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>

#include "app.h"
#include "dir.h"
#include "preview.h"
#include "tpool.h"

struct res_t;

typedef struct resq_t {
	struct res_t *head;
	struct res_t *tail;
	pthread_mutex_t mutex;
	ev_async *watcher;
} resq_t;

extern tpool_t *async_tm;
extern resq_t async_results;

/*
 * Returns a `res_t` from `queue` if it is non-empty, `NULL` otherwise.
 */
struct res_t *queue_get(resq_t *queue);

/*
 * Destroy all results that remain in the queue.
 */
void queue_deinit(resq_t *queue);

/*
 * Process the result and free its resources.
 */
void res_callback(struct res_t *res, app_t *app);

/*
 * Check the modification time of `dir` on disk. Possibly generates a `res_t`
 * to trigger reloading the directory.
 */
void async_dir_check(dir_t *dir);

/*
 * Reloads `dir` from disk after `delay` milliseconds.
 */
void async_dir_load_delayed(dir_t *dir, bool dircounts, int delay /* millis */);

/*
 * Loads `dir` from disk.
 */
#define async_dir_load(dir, dircounts) async_dir_load_delayed(dir, dircounts, -1)

/*
 * Check the modification time of `pv` on disk. Possibly generates a `res_t` to
 * trigger reloading the preview.
 */
void async_preview_check(preview_t *pv);

/*
 * Reloads preview of the file at `path` with `nrow` lines from disk.
 */
void async_preview_load(const char *path, int nrow);
