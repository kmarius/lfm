#include "input.h"

#include "cmdline.h"
#include "config.h"
#include "defs.h"
#include "fm.h"
#include "hooks.h"
#include "keys.h"
#include "lfm.h"
#include "log.h"
#include "loop.h"
#include "lua/lfmlua.h"
#include "macro.h"
#include "mode.h"
#include "search.h"
#include "selection.h"
#include "stcutil.h"
#include "trie.h"
#include "ui.h"
#include "util.h"

#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <stc/cstr.h>

#include <wctype.h>

#define MAP_MAX_LENGTH 8

static void stdin_cb(EV_P_ ev_io *w, i32 revents);
static void input_buffer_cb(EV_P_ ev_idle *w, i32 revents);
static void map_clear_timer_cb(EV_P_ ev_timer *w, i32 revents);
static void map_suggestion_timer_cb(EV_P_ ev_timer *w, i32 revents);

void input_init(struct input_ctx *ctx, Lfm *lfm) {
  ctx->input_watcher.data = lfm;

  ev_idle_init(&ctx->input_buffer_watcher, input_buffer_cb);
  ctx->input_buffer_watcher.data = lfm;
  // increase the priority so we handle input before redrawing
  ev_set_priority(&ctx->input_buffer_watcher, 1);

  f64 repeat = (f64)cfg.map_clear_delay / 1000.0;
  ev_timer_init(&ctx->map_clear_timer, map_clear_timer_cb, 0, repeat);
  ctx->map_clear_timer.data = lfm;

  repeat = (f64)cfg.map_suggestion_delay / 1000.0;
  ev_timer_init(&ctx->map_suggestion_timer, map_suggestion_timer_cb, 0, repeat);
  ctx->map_suggestion_timer.data = lfm;
}

void input_deinit(struct input_ctx *ctx) {
  vec_input_drop(&ctx->seq);
  queue_input_drop(&ctx->input_buffer);
}

void input_resume(struct input_ctx *ctx, struct notcurses *nc) {
  int fd = notcurses_inputready_fd(nc);
  ev_io_init(&ctx->input_watcher, stdin_cb, fd, EV_READ);
  ev_io_start(event_loop, &ctx->input_watcher);
}

void input_suspend(struct input_ctx *ctx) {
  ev_io_stop(event_loop, &ctx->input_watcher);
  ev_timer_stop(event_loop, &ctx->map_suggestion_timer);
  ev_timer_stop(event_loop, &ctx->map_clear_timer);
}

static void stdin_cb(EV_P_ ev_io *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  struct input_ctx *ctx = &lfm->ui.input;

  ncinput in;
  while (notcurses_get_nblock(lfm->ui.nc, &in) != (u32)-1) {
    if (in.id == 0)
      break;

    if (unlikely(in.id == NCKEY_EOF)) {
      log_debug("received EOF, quitting");
      lfm_quit(lfm, 0);
      return;
    }

    // to emulate legacy with the kitty protocol (once it works in notcurses)
    // if (in.evtype == NCTYPE_RELEASE) {
    //   continue;
    // }
    // if (in.id >= NCKEY_LSHIFT && in.id <= NCKEY_L5SHIFT) {
    //   continue;
    // }

    if (in.id == NCKEY_FOCUS) {
      LFM_RUN_HOOK(lfm, LFM_HOOK_FOCUSGAINED);
    } else if (in.id == NCKEY_UNFOCUS) {
      LFM_RUN_HOOK(lfm, LFM_HOOK_FOCUSLOST);
    } else {
      log_trace("id=%d shift=%d ctrl=%d alt=%d type=%d utf8=%s", in.id,
                ncinput_shift_p(&in), ncinput_ctrl_p(&in), ncinput_alt_p(&in),
                in.evtype,
                in.utf8[1] != 0 || isprint(in.utf8[0]) ? in.utf8 : "");
      input_t key = ncinput_to_input(&in);
      if (macro_recording)
        macro_add_key(key);
      input_handle_key(ctx, lfm, key);
    }
  }
}

// start of a new sequence
static inline void input_init_sequence(struct input_ctx *ctx, Lfm *lfm) {
  ctx->cur = lfm->current_mode->maps;
  ctx->cur_base =
      lfm->current_mode->is_input ? ctx->input_maps : ctx->normal_maps;
  // null one of the tries for the two base modes
  if (ctx->cur == ctx->cur_base)
    ctx->cur_base = NULL;
  ctx->count = -1;
}

// clear the current key sequence and reset the state
static inline void input_clear(struct input_ctx *ctx, Ui *ui) {
  ctx->state = INPUT_STATE_IDLE;
  ctx->cur = NULL;
  ctx->cur_base = NULL;
  ctx->count = -1;
  ui_menu_hide(ui);
  if (!vec_input_is_empty(&ctx->seq)) {
    vec_input_clear(&ctx->seq);
    ui_redraw(ui, REDRAW_CMDLINE);
  }
}

static inline void accumulate_digit(struct input_ctx *ctx, Ui *ui, input_t in) {
  if (ctx->count < 0) {
    ctx->count = in - '0';
  } else {
    ctx->count = ctx->count * 10 + in - '0';
  }
  if (ctx->count > 0) {
    vec_input_push(&ctx->seq, in);
    ui_redraw(ui, REDRAW_CMDLINE);
  }
}

// escape in idle state clears search highlight, the selection and paste
// buffers, the menu overlay and possibly exits the current mode.
static inline void handle_esc_idle(Lfm *lfm) {
  Ui *ui = &lfm->ui;
  Fm *fm = &lfm->fm;
  u32 mode = 0;

  if (selection_clear(fm))
    mode |= REDRAW_FM;
  if (paste_buffer_clear(fm)) {
    LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
    mode |= REDRAW_FM;
  }
  search_nohighlight(lfm);
  ui_menu_hide(ui);
  mode_on_esc(lfm->current_mode, lfm);
  lfm_mode_normal(lfm);

  if (ui->show_message) {
    ui->show_message = false;
    mode |= REDRAW_CMDLINE;
  }
  ui_redraw(ui, mode);
}

static inline void log_unmapped_sequence(struct input_ctx *ctx, input_t in) {
  vec_input_push(&ctx->seq, in);
  char buf[256];
  usize len;
  u32 i = 0;
  c_foreach(it, vec_input, ctx->seq) {
    const char *str = input_to_key_name(*it.ref, &len);
    if (i + len > sizeof buf - 1)
      break;
    memcpy(buf + i, str, len);
    i += len;
  }
  buf[i] = 0;
  log_debug("unmapped sequence: %s (last: id=%d shift=%d ctrl=%d alt=%d)", buf,
            ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in));
}

// returns true if any match (partial or leaf), false if no match
static inline bool process_sequence_key(struct input_ctx *ctx, Lfm *lfm,
                                        input_t in) {
  Trie *next_cur = ctx->cur ? trie_find_child(ctx->cur, in) : NULL;
  Trie *next_base = ctx->cur_base ? trie_find_child(ctx->cur_base, in) : NULL;

  // check for leaf match - cur takes precedence
  if (next_cur && next_cur->ref) {
    i32 ref = next_cur->ref;
    i32 count = ctx->count;
    input_clear(ctx, &lfm->ui);
    lfm_lua_cb_with_count(lfm->L, ref, count);
    return true;
  }
  if (next_base && next_base->ref) {
    i32 ref = next_base->ref;
    i32 count = ctx->count;
    input_clear(ctx, &lfm->ui);
    lfm_lua_cb_with_count(lfm->L, ref, count);
    return true;
  }

  // no match
  if (!next_cur && !next_base)
    return false;

  // partial match
  ctx->cur = next_cur;
  ctx->cur_base = next_base;
  ctx->state = INPUT_STATE_SEQUENCE;
  vec_input_push(&ctx->seq, in);
  ui_redraw(&lfm->ui, REDRAW_CMDLINE);
  ev_timer_again(event_loop, &ctx->map_clear_timer);
  ev_timer_again(event_loop, &ctx->map_suggestion_timer);
  return true;
}

// add a character to the command line in input mode
static inline void input_add_char(Ui *ui, Lfm *lfm, input_t in) {
  char buf[MB_LEN_MAX + 1];
  i32 n = wctomb(buf, in);
  if (unlikely(n < 0)) {
    log_error("invalid input: %lu", in);
    n = 0;
  }
  buf[n] = '\0';
  if (cmdline_insert(&ui->cmdline, zsview_from_n(buf, n))) {
    mode_on_change(lfm->current_mode, lfm);
    ui_redraw(ui, REDRAW_CMDLINE);
  }
}

static inline void handle_normal_mode_key(struct input_ctx *ctx, Lfm *lfm,
                                          input_t in) {
  Ui *ui = &lfm->ui;

  if (in == NCKEY_ESC) {
    if (ctx->state == INPUT_STATE_IDLE)
      handle_esc_idle(lfm);
    else
      input_clear(ctx, ui);
    return;
  }

  switch (ctx->state) {
  case INPUT_STATE_IDLE:
    input_init_sequence(ctx, lfm);
    if ('0' < in && in <= '9') {
      ctx->state = INPUT_STATE_COUNT;
      accumulate_digit(ctx, ui, in);
      return;
    }
    if (!process_sequence_key(ctx, lfm, in)) {
      log_unmapped_sequence(ctx, in);
      input_clear(ctx, ui);
    }
    break;

  case INPUT_STATE_COUNT:
    if ('0' <= in && in <= '9') {
      accumulate_digit(ctx, ui, in);
      return;
    }
    if (!process_sequence_key(ctx, lfm, in)) {
      log_unmapped_sequence(ctx, in);
      input_clear(ctx, ui);
    }
    break;

  case INPUT_STATE_SEQUENCE:
    if (!process_sequence_key(ctx, lfm, in)) {
      log_unmapped_sequence(ctx, in);
      input_clear(ctx, ui);
    }
    break;
  }
}

static inline void handle_input_mode_key(struct input_ctx *ctx, Lfm *lfm,
                                         input_t in) {
  Ui *ui = &lfm->ui;

  // reset state and switch to normal mode
  if (in == NCKEY_ESC) {
    mode_on_esc(lfm->current_mode, lfm);
    input_clear(ctx, ui);
    lfm_mode_normal(lfm);
    return;
  }

  // handle return key
  if (in == NCKEY_ENTER) {
    zsview line = cmdline_get(&ui->cmdline);
    input_clear(ctx, ui);
    mode_on_return(lfm->current_mode, lfm, line);
    return;
  }

  // initialize on first key
  if (ctx->state == INPUT_STATE_IDLE)
    input_init_sequence(ctx, lfm);

  if (ctx->state == INPUT_STATE_SEQUENCE) {
    if (!process_sequence_key(ctx, lfm, in)) {
      log_unmapped_sequence(ctx, in);
      input_clear(ctx, ui);
    }
    return;
  }

  // in IDLE state, attempt to find a key sequence
  if (process_sequence_key(ctx, lfm, in))
    return;

  input_clear(ctx, ui);
  if (iswprint(in))
    input_add_char(ui, lfm, in);
}

void input_handle_key(struct input_ctx *ctx, Lfm *lfm, input_t in) {
  if (unlikely(in == CTRL('Q') || in == CTRL('q'))) {
    log_debug("received ctrl-q, quitting");
    lfm_quit(lfm, 0);
    return;
  }

  ev_timer_stop(event_loop, &ctx->map_clear_timer);
  ev_timer_stop(event_loop, &ctx->map_suggestion_timer);

  if (lfm->current_mode->is_input) {
    handle_input_mode_key(ctx, lfm, in);
  } else {
    handle_normal_mode_key(ctx, lfm, in);
  }
}

void input_buffer_add(struct input_ctx *ctx, input_t in) {
  queue_input_push(&ctx->input_buffer, in);
  ev_idle_start(event_loop, &ctx->input_buffer_watcher);
}

static void input_buffer_cb(EV_P_ ev_idle *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  struct input_ctx *ctx = &lfm->ui.input;

  ev_idle_stop(EV_A_ w);
  while (!queue_input_is_empty(&ctx->input_buffer)) {
    input_t in = *queue_input_front(&ctx->input_buffer);
    queue_input_pop(&ctx->input_buffer);
    input_handle_key(ctx, lfm, in);
  }
}

static void map_clear_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  input_clear(&lfm->ui.input, &lfm->ui);
  ev_timer_stop(EV_A_ w);
}

static void map_suggestion_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  ev_timer_stop(EV_A_ w);
  Lfm *lfm = w->data;
  struct input_ctx *ctx = &lfm->ui.input;

  vec_trie maps = {0};
  vec_trie base_maps = {0};

  if (ctx->cur)
    maps = trie_collect_leaves(ctx->cur, true);
  if (ctx->cur_base)
    base_maps = trie_collect_leaves(ctx->cur_base, true);
  vec_trie_sort(&maps);
  vec_trie_sort(&base_maps);

  // merge base_maps into maps, skipping shadowed entries
  // we could actually merge instead of nested loop.
  c_foreach(b, vec_trie, base_maps) {
    bool shadowed = false;
    c_foreach(m, vec_trie, maps) {
      if (cstr_cmp(&(*m.ref)->keys, &(*b.ref)->keys) > 0)
        break;
      if (hascaseprefix(cstr_str(&(*b.ref)->keys), cstr_str(&(*m.ref)->keys))) {
        shadowed = true;
        break;
      }
    }
    if (!shadowed)
      vec_trie_push(&maps, *b.ref);
  }
  vec_trie_drop(&base_maps);
  vec_trie_sort(&maps);

  vec_cstr lines = vec_cstr_with_capacity(vec_trie_size(&maps) + 1);
  // bold header
  vec_cstr_emplace(&lines, "\033[1mkeys\tcommand\033[0m");
  c_foreach(it, vec_trie, maps) {
    Trie *map = *it.ref;
    zsview keys = cstr_zv(&map->keys);
    zsview desc = cstr_zv(&map->desc);
    cstr line = cstr_with_capacity(keys.size + desc.size + 1);
    cstr_append_zv(&line, keys);
    cstr_append_n(&line, "\t", 1);
    cstr_append_zv(&line, desc);
    vec_cstr_push_back(&lines, line);
  }
  vec_trie_drop(&maps);
  ui_menu_show(&lfm->ui, &lines, 0);
}

i32 input_map(Trie *trie, zsview keys, i32 ref, zsview desc, i32 *out_ref) {
  input_t buf[MAP_MAX_LENGTH + 1];
  i32 status = key_names_to_input(keys, buf, MAP_MAX_LENGTH + 1);
  if (status < 0) {
    return status;
  }
  log_trace("input_map %s %d %s", keys.str, ref, desc.str);
  if (ref) {
    *out_ref = trie_insert(trie, buf, ref, keys, desc);
  } else {
    *out_ref = trie_remove(trie, buf);
  }
  return 0;
}
