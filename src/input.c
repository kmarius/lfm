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

#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>
#include <stc/cstr.h>

#include <wctype.h>

#define MAP_MAX_LENGTH 8

static void stdin_cb(EV_P_ ev_io *w, i32 revents);
static void input_buffer_cb(EV_P_ ev_idle *w, i32 revents);
static void map_clear_timer_cb(EV_P_ ev_timer *w, i32 revents);
static void map_suggestion_timer_cb(EV_P_ ev_timer *w, i32 revents);

void input_init(Lfm *lfm) {
  Ui *ui = &lfm->ui;
  ui->input_watcher.data = lfm;

  ev_idle_init(&ui->input_buffer_watcher, input_buffer_cb);
  ui->input_buffer_watcher.data = lfm;
  // increase the priority so we handle input before redrawing
  ev_set_priority(&ui->input_buffer_watcher, 1);

  f64 repeat = (f64)cfg.map_clear_delay / 1000.0;
  ev_timer_init(&ui->map_clear_timer, map_clear_timer_cb, 0, repeat);
  ui->map_clear_timer.data = lfm;

  repeat = (f64)cfg.map_suggestion_delay / 1000.0;
  ev_timer_init(&ui->map_suggestion_timer, map_suggestion_timer_cb, 0, repeat);
  ui->map_suggestion_timer.data = lfm;
}

void input_deinit(Lfm *lfm) {
  vec_input_drop(&lfm->ui.maps.seq);
}

void input_resume(Lfm *lfm) {
  ev_io_init(&lfm->ui.input_watcher, stdin_cb,
             notcurses_inputready_fd(lfm->ui.nc), EV_READ);
  ev_io_start(event_loop, &lfm->ui.input_watcher);
}

void input_suspend(Lfm *lfm) {
  ev_io_stop(event_loop, &lfm->ui.input_watcher);
  ev_timer_stop(event_loop, &lfm->ui.map_suggestion_timer);
  ev_timer_stop(event_loop, &lfm->ui.map_clear_timer);
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

static void stdin_cb(EV_P_ ev_io *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  ncinput in;

  while (notcurses_get_nblock(lfm->ui.nc, &in) != (u32)-1) {
    if (in.id == 0)
      break;

    if (in.id == NCKEY_EOF) {
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
                in.shift, in.ctrl, in.alt, in.evtype,
                in.utf8[1] != 0 || isprint(in.utf8[0]) ? in.utf8 : "");
      input_t key = ncinput_to_input(&in);
      if (macro_recording) {
        macro_add_key(key);
      }
      input_handle_key(lfm, key);
    }
  }
}

// clear the current key sequence and reset the state
static inline void input_clear(Lfm *lfm) {
  Ui *ui = &lfm->ui;
  ui->maps.cur = NULL;
  ui_menu_hide(ui);
  if (!vec_input_is_empty(&ui->maps.seq)) {
    vec_input_clear(&ui->maps.seq);
    ui_redraw(ui, REDRAW_CMDLINE);
  }
}

void input_buffer_add(struct Lfm *lfm, input_t in) {
  queue_input_push(&lfm->ui.input_buffer, in);
  ev_idle_start(event_loop, &lfm->ui.input_buffer_watcher);
}

static void input_buffer_cb(EV_P_ ev_idle *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  ev_idle_stop(EV_A_ w);
  while (!queue_input_is_empty(&lfm->ui.input_buffer)) {
    input_t in = *queue_input_front(&lfm->ui.input_buffer);
    queue_input_pop(&lfm->ui.input_buffer);
    input_handle_key(lfm, in);
  }
}

static inline void handle_non_input_mode_key(Lfm *lfm, input_t in) {
  Ui *ui = &lfm->ui;
  Fm *fm = &lfm->fm;

  // reset pointer to the trie's root
  if (!lfm->ui.maps.cur) {
    lfm->ui.maps.cur = lfm->current_mode->maps;
    vec_input_clear(&lfm->ui.maps.seq);
    lfm->ui.maps.count = -1;
    lfm->ui.maps.accept_count = true;
  }

  // accumulate numbers to pass as a count to the next command
  if (lfm->ui.maps.accept_count && '0' <= in && in <= '9') {
    if (lfm->ui.maps.count < 0) {
      lfm->ui.maps.count = in - '0';
    } else {
      lfm->ui.maps.count = lfm->ui.maps.count * 10 + in - '0';
    }
    if (lfm->ui.maps.count > 0) {
      vec_input_push(&lfm->ui.maps.seq, in);
      ui_redraw(ui, REDRAW_CMDLINE);
    }
    return;
  }

  lfm->ui.maps.cur = trie_find_child(lfm->ui.maps.cur, in);

  // handle escape key
  if (in == NCKEY_ESC) {
    u32 mode = 0;
    if (!vec_input_is_empty(&lfm->ui.maps.seq)) {
      input_clear(lfm);
    } else {
      if (selection_clear(&lfm->fm))
        mode |= REDRAW_FM;
      if (paste_buffer_clear(fm)) {
        LFM_RUN_HOOK(lfm, LFM_HOOK_PASTEBUF);
        mode |= REDRAW_FM;
      }
      search_nohighlight(lfm);
      ui_menu_hide(&lfm->ui);
      mode_on_esc(lfm->current_mode, lfm);
      lfm_mode_normal(lfm);
    }
    if (ui->show_message) {
      ui->show_message = false;
      mode |= REDRAW_CMDLINE;
    }
    ui_redraw(ui, mode);
    return;
  }

  // key sequence is not in the trie, log and reset state
  if (!lfm->ui.maps.cur) {
    vec_input_push(&lfm->ui.maps.seq, in);
    char buf[256];
    usize len;
    u32 i = 0;
    c_foreach(it, vec_input, lfm->ui.maps.seq) {
      const char *str = input_to_key_name(*it.ref, &len);
      if (i + len > sizeof buf - 1)
        break;
      memcpy(buf + i, str, len);
      i += len;
    }
    buf[i] = 0;
    log_debug("unmapped key sequence: %s (id=%d shift=%d ctrl=%d alt=%d)", buf,
              ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in));
    input_clear(lfm);
    return;
  }

  // key sequence ends at a leaf in the trie, reset state and execute
  if (lfm->ui.maps.cur->is_leaf) {
    i32 ref = lfm->ui.maps.cur->ref;
    input_clear(lfm);
    lfm_lua_cb_with_count(lfm->L, ref, lfm->ui.maps.count);
    return;
  }

  // accumulate input and wait for more
  vec_input_push(&lfm->ui.maps.seq, in);
  lfm->ui.maps.accept_count = false;
  ui_redraw(ui, REDRAW_CMDLINE);

  ev_timer_again(event_loop, &lfm->ui.map_clear_timer);
  ev_timer_again(event_loop, &lfm->ui.map_suggestion_timer);
}

static inline void handle_input_mode_key(Lfm *lfm, input_t in) {
  Ui *ui = &lfm->ui;

  // reset the buffer/trie only if no mode map and no input map are possible
  if (!lfm->ui.maps.cur && !lfm->ui.maps.cur_input) {
    lfm->ui.maps.cur = lfm->current_mode->maps;
    vec_input_clear(&lfm->ui.maps.seq);
    lfm->ui.maps.count = -1;
    lfm->ui.maps.accept_count = true;
  }

  lfm->ui.maps.cur = trie_find_child(lfm->ui.maps.cur, in);

  // TODO: currently, if all but the last keys match the mapping of in a mode,
  // and the last one is printable, it will be added to the input field

  // handle esc, reset state and switch to normal mode
  if (in == NCKEY_ESC) {
    mode_on_esc(lfm->current_mode, lfm);
    input_clear(lfm);
    lfm_mode_normal(lfm);
    return;
  }

  // handle return key
  if (in == NCKEY_ENTER) {
    zsview line = cmdline_get(&ui->cmdline);
    input_clear(lfm);
    mode_on_return(lfm->current_mode, lfm, line);
    return;
  }

  if (lfm->ui.maps.cur) {
    // current key sequence is a prefix/full match of a mode mapping, always
    // taking precedence
    if (lfm->ui.maps.cur->ref) {
      i32 ref = lfm->ui.maps.cur->ref;
      lfm->ui.maps.cur = NULL;
      lfm_lua_cb_with_count(lfm->L, ref, -1);
    }
    return;
  }

  if (!iswprint(in) && lfm->ui.maps.cur_input == NULL) {
    // If the character is not printable, check for input maps.
    // this way one can map e.g. <c-d>a in insert mode without 'a' getting added
    // to the command line
    lfm->ui.maps.cur_input = lfm->ui.maps.input;
  }

  // if input map trie is active (started by a non-printable key), check for a
  // map even if the key is printable
  if (lfm->ui.maps.cur_input != NULL) {
    lfm->ui.maps.cur_input = trie_find_child(lfm->ui.maps.cur_input, in);
    if (lfm->ui.maps.cur_input) {
      // current key sequence is a prefix/full match of a mode mapping,
      // always taking precedence
      if (lfm->ui.maps.cur_input->ref) {
        i32 ref = lfm->ui.maps.cur_input->ref;
        lfm->ui.maps.cur_input = NULL;
        lfm_lua_cb_with_count(lfm->L, ref, -1);
      } else {
        // TODO: we could show possible mappings on screen
      }
    }
    return;
  }

  // otherwise, add the input to the command line
  assert(iswprint(in));

  char buf[MB_LEN_MAX + 1];
  i32 n = wctomb(buf, in);
  if (n < 0) {
    log_error("invalid input: %lu", in);
    n = 0;
  }
  buf[n] = '\0';
  if (cmdline_insert(&ui->cmdline, zsview_from_n(buf, n))) {
    mode_on_change(lfm->current_mode, lfm);
    ui_redraw(ui, REDRAW_CMDLINE);
  }
}

void input_handle_key(Lfm *lfm, input_t in) {
  if (in == CTRL('Q') || in == CTRL('q')) {
    log_debug("received ctrl-q, quitting");
    lfm_quit(lfm, 0);
    return;
  }

  ev_timer_stop(event_loop, &lfm->ui.map_clear_timer);
  ev_timer_stop(event_loop, &lfm->ui.map_suggestion_timer);

  if (lfm->current_mode->is_input) {
    handle_input_mode_key(lfm, in);
  } else {
    handle_non_input_mode_key(lfm, in);
  }
}

static void map_clear_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  Lfm *lfm = w->data;
  input_clear(lfm);
  ev_timer_stop(EV_A_ w);
}

static void map_suggestion_timer_cb(EV_P_ ev_timer *w, i32 revents) {
  (void)revents;
  ev_timer_stop(EV_A_ w);
  Lfm *lfm = w->data;

  vec_trie maps = trie_collect_leaves(lfm->ui.maps.cur, true);
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
