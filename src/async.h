#ifndef ASYNC_H
#define ASYNC_H

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>

#include "app.h"
#include "dir.h"
#include "preview.h"
#include "tpool.h"

enum result_e { RES_DIR_UPDATE, RES_DIR_CHECK, RES_PREVIEW, RES_PREVIEW_CHECK, };

typedef struct res_t res_t;
typedef void (*async_cb)(struct res_t*, struct app_t*);

struct res_t {
	enum result_e type; /* only used to free on shutdown */
	async_cb cb;
	union {
		struct {
			dir_t *dir;
			dir_t *update;
		};
		preview_t *preview;
		struct {
			char *path;
			int nrow;
		};
	};
	struct res_t *next;
};

typedef struct resq_t {
	struct res_t *head;
	struct res_t *tail;
	pthread_mutex_t mutex;
	ev_async *watcher;
} resq_t;

extern tpool_t *async_tm;
extern resq_t async_results;

/*
 * Get result from the result `queue`. Result will be written to `result`.
 * Returns `true` if a result was received, `false` otherwise (i.e. if the
 * queue was empty).
 */
bool queue_get(resq_t *queue, res_t *result);

/*
 * Free all results that remain in the queue.
 */
void queue_deinit(resq_t *queue);

/*
 * Check the modification time of `dir` on disk. Generates a result of type
 * `RES_DIR_CHECK` if the directory needs to be reloaded.
 */
void async_dir_check(dir_t *dir);

/*
 * Reloads `dir` from disk after `delay` milliseconds. Generates a result of
 * type `RES_DIR_UPDATE`.
 */
void async_dir_load_delayed(dir_t *dir, bool dircounts, int delay /* millis */);

/*
 * Reloads `dir` from disk. Generates a result of type `RES_DIR_UPDATE`.
 */
#define async_dir_load(dir, dircounts) async_dir_load_delayed(dir, dircounts, -1)

/*
 * Check the modification time of `pv` on disk. Generates a result of type
 * `RES_PREVIEW_CHECK` if the preview needs to be reloaded.
 */
void async_preview_check(preview_t *pv);

/*
 * Reloads preview of the file at `path` with `nrow` lines from disk. Generates
 * a result of type `RES_PREVIEW`.
 */
void async_preview_load(const char *path, int nrow);

#undef cb

#endif /* ASYNC_H */
