#pragma once

#include "dir.h"
#include "hashtab.h"
#include "preview.h"

struct ev_loop;

void loader_init(void *lfm);
void loader_deinit();
// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule();

Dir *loader_dir_from_path(const char *path);
void loader_dir_reload(Dir *dir);
Hashtab *loader_dir_hashtab();
void loader_drop_dir_cache();


Preview *loader_preview_from_path(const char *path, bool image);
void loader_preview_reload(Preview *pv);
Hashtab *loader_pv_hashtab();
void loader_drop_preview_cache();
