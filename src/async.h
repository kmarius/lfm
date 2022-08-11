#pragma once

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "dir.h"
#include "preview.h"
#include "tpool.h"

void async_init(App *app);

void async_deinit();

// Check the modification time of `dir` on disk. Possibly generates a `res_t`
// to trigger reloading the directory.
void async_dir_check(Dir *dir);

// Reloads `dir` from disk.
void async_dir_load(Dir *dir, bool dircounts);

// Check the modification time of `pv` on disk. Possibly generates a `res_t` to
// trigger reloading the preview.
void async_preview_check(Preview *pv);

// Reloads preview of the file at `path` with `nrow` lines from disk.
void async_preview_load(Preview *pv, uint32_t nrow);
