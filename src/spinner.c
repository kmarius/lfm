#include "spinner.h"

#include <limits.h>
#include <notcurses/notcurses.h>

static void spinner_draw(EV_P_ ev_timer *w, int revents);

struct spinner *spinner_init(struct spinner *spinner, const char *chars,
                             struct ev_loop *loop, struct ncplane *n) {
  ev_timer_init(&spinner->timer, spinner_draw, 0.0, SPINNER_INTERVAL / 1000.0);
  spinner->chars = chars;
  spinner->len = strlen(chars);
  spinner->i = 0;
  spinner->n = n;
  spinner->loop = loop;
  return spinner;
}

static void spinner_draw(EV_P_ ev_timer *w, int revents) {
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

void spinner_on(struct spinner *spinner, unsigned int y, unsigned int x,
                uint64_t channels, uint16_t style) {
  spinner->y = y;
  spinner->x = x;
  spinner->channels = channels;
  spinner->style = style;
  if (!ev_is_active(spinner))
    ev_timer_start(spinner->loop, &spinner->timer);
}
