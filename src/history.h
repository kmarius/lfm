#pragma once

/* TODO: integrate this with cmdline instead of Ui (on 2022-01-25) */

typedef struct history_s {
  struct history_entry *vec;
  struct history_entry *ptr;
} History;

void history_load(History *h, const char *path);
void history_write(History *h, const char *path);
void history_append(History *h, const char *prefix, const char *line);
void history_reset(History *h);
void history_deinit(History *h);
const char *history_next(History *h);
const char *history_prev(History *h);
