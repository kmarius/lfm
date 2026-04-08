#include "spinner.h"

#include <notcurses/notcurses.h>

#include <limits.h>

static void spinner_draw(EV_P_ ev_timer *w, i32 revents);

struct spinner *spinner_init(struct spinner *spinner, const char *chars,
                             struct ev_loop *loop) {
  ev_timer_init(&spinner->timer, spinner_draw, 0.0, SPINNER_INTERVAL / 1000.0);
  spinner->chars = chars;
  spinner->len = strlen(chars);
  spinner->i = 0;
  spinner->loop = loop;
  return spinner;
}

static void spinner_draw(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  struct spinner *spinner = (struct spinner *)w;
  struct ncplane *n = spinner->n;
  ncplane_set_channels(n, spinner->channels);
  ncplane_set_styles(n, spinner->style);
  spinner_draw_char(spinner);
  ncplane_putnstr_yx(n, spinner->y, spinner->x, 1, &spinner->chars[spinner->i]);
  notcurses_render(ncplane_notcurses(n));
  spinner->i += mbtowc(NULL, &spinner->chars[spinner->i], MB_LEN_MAX);
  spinner->i %= spinner->len;
}

void spinner_on(struct spinner *spinner, i32 y, i32 x, u64 channels, u16 style,
                struct ncplane *n) {
  spinner->y = y;
  spinner->x = x;
  spinner->channels = channels;
  spinner->style = style;
  spinner->n = n;
  if (!ev_is_active(spinner))
    ev_timer_start(spinner->loop, &spinner->timer);
}
