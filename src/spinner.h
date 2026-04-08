#pragma once

#include "defs.h"

#include <ev.h>
#include <notcurses/notcurses.h>

#define SPINNER_INTERVAL 80

struct spinner {
  ev_timer timer;
  i32 y, x;
  u64 channels;
  u16 style;
  const char *chars;
  i32 len;
  i32 i;
  struct ev_loop *loop;
  struct ncplane *n;
};

// const char spinners[] = "◢◣◤◥";
// const char spinners[] = "▁▂▃▄▅▆▇█▇▆▅▄▃▁";
// const char spinners[] = "◰◳◲◱";
static const char spinner_chars[] = "⣾⣽⣻⢿⡿⣟⣯⣷";
// braille random: 0x2800 - 0x28ff

struct spinner *spinner_init(struct spinner *spinner, const char *chars,
                             struct ev_loop *loop);

void spinner_on(struct spinner *spinner, i32 y, i32 x, u64 channels, u16 style,
                struct ncplane *n);

static inline void spinner_draw_char(struct spinner *spinner) {
  ncplane_putnstr_yx(spinner->n, spinner->y, spinner->x, 1,
                     &spinner->chars[spinner->i]);
}

static inline void spinner_off(struct spinner *spinner) {
  if (ev_is_active(&spinner->timer)) {
    ev_timer_stop(spinner->loop, &spinner->timer);
    spinner->i = 0;
  }
}
