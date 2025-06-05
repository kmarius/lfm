#pragma once

#include <ev.h>
#include <notcurses/notcurses.h>
#include <stdint.h>

#define SPINNER_INTERVAL 80

struct spinner {
  ev_timer timer;
  unsigned int y, x;
  uint64_t channels;
  uint16_t style;
  const char *chars;
  int len;
  int i;
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

void spinner_on(struct spinner *spinner, unsigned int y, unsigned int x,
                uint64_t channels, uint16_t style, struct ncplane *n);

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
