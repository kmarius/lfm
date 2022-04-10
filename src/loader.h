#pragma once

#include "dir.h"
#include "hashtab.h"

#define LOADER_TAB_SIZE 1024  // size of the hashtab used as cache

struct ev_loop;

void loader_init(struct ev_loop *loop);
void loader_deinit();

void loader_reload(Dir *dir);
Dir *loader_load_path(const char *path);

Hashtab *loader_hashtab();
void loader_drop_cache();

// Reschedule reloads, e.g. when timeout/delay is changed.
void loader_reschedule();
