#include "input.h"

#include "cmdline.h"
#include "config.h"
#include "fm.h"
#include "hooks.h"
#include "keys.h"
#include "lfm.h"
#include "log.h"
#include "lua/lfmlua.h"
#include "macro.h"
#include "mode.h"
#include "search.h"
#include "stc/cstr.h"
#include "stcutil.h"
#include "trie.h"
#include "ui.h"
#include "util.h"

#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>

#include <wctype.h>

#define MAP_MAX_LENGTH 8

static void map_clear_timer_cb(EV_P_ ev_timer *w, int revents);
static void map_suggestion_timer_cb(EV_P_ ev_timer *w, int revents);
static void stdin_cb(EV_P_ ev_io *w, int revents);

void input_init(Lfm *lfm) {
  lfm->ui.input_watcher.data = lfm;

  ev_timer_init(&lfm->ui.map_clear_timer, map_clear_timer_cb, 0, 0);
  lfm->ui.map_clear_timer.data = lfm;

  ev_timer_init(&lfm->ui.map_suggestion_timer, map_suggestion_timer_cb, 0, 0);
  lfm->ui.map_suggestion_timer.data = lfm;

  macros_init();
}

void input_deinit(Lfm *lfm) {
  macros_deinit();
  vec_input_drop(&lfm->ui.maps.seq);
  ev_timer_stop(lfm->loop, &lfm->ui.map_clear_timer);
}

void input_resume(Lfm *lfm) {
  ev_io_init(&lfm->ui.input_watcher, stdin_cb,
             notcurses_inputready_fd(lfm->ui.nc), EV_READ);
  ev_io_start(lfm->loop, &lfm->ui.input_watcher);
}

void input_suspend(Lfm *lfm) {
  ev_io_stop(lfm->loop, &lfm->ui.input_watcher);
}

int input_map(Trie *trie, zsview keys, int ref, zsview desc, int *out_ref) {
  input_t buf[MAP_MAX_LENGTH + 1];
  int status = key_names_to_input(keys, buf, MAP_MAX_LENGTH + 1);
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

static void stdin_cb(EV_P_ ev_io *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  ncinput in;

  while (notcurses_get_nblock(lfm->ui.nc, &in) != (uint32_t)-1) {
    if (in.id == 0) {
      break;
    }

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
      lfm_run_hook(lfm, LFM_HOOK_FOCUSGAINED);
    } else if (in.id == NCKEY_UNFOCUS) {
      lfm_run_hook(lfm, LFM_HOOK_FOCUSLOST);
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

  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

// clear keys in the input buffer
static inline void input_clear(Lfm *lfm) {
  Ui *ui = &lfm->ui;
  ui->maps.cur = NULL;
  ui_menu_hide(ui);
  if (vec_input_size(&ui->maps.seq) > 0) {
    ui_redraw(ui, REDRAW_CMDLINE);
    vec_input_clear(&ui->maps.seq);
  }
}

void input_handle_key(Lfm *lfm, input_t in) {
  Ui *ui = &lfm->ui;
  Fm *fm = &lfm->fm;

  if (in == CTRL('Q')) {
    log_debug("received ctrl-q, quitting");
    lfm_quit(lfm, 0);
    return;
  }

  ev_timer_stop(lfm->loop, &lfm->ui.map_clear_timer);
  ev_timer_stop(lfm->loop, &lfm->ui.map_suggestion_timer);
  if (lfm->current_mode->is_input) {
    if (!lfm->ui.maps.cur && !lfm->ui.maps.cur_input) {
      // reset the buffer/trie only if no mode map and no input map are possible
      lfm->ui.maps.cur = lfm->current_mode->maps;
      vec_input_clear(&lfm->ui.maps.seq);
      lfm->ui.maps.count = -1;
      lfm->ui.maps.accept_count = true;
    }
    lfm->ui.maps.cur = trie_find_child(lfm->ui.maps.cur, in);
    // TODO: currently, if all but the last keys match the mapping of in a mode,
    // and the last one is printable, it will be added to the input field
    if (in == NCKEY_ESC) {
      // escape key pressed, switch to normal
      mode_on_esc(lfm->current_mode, lfm);
      input_clear(lfm);
      lfm_mode_normal(lfm);
    } else if (in == NCKEY_ENTER) {
      // return key pressed, call the callback in the mode
      zsview line = cmdline_get(&ui->cmdline);
      input_clear(lfm);
      mode_on_return(lfm->current_mode, lfm, line);
    } else if (lfm->ui.maps.cur) {
      // current key sequence is a prefix/full match of a mode mapping, always
      // taking precedence
      if (lfm->ui.maps.cur->ref) {
        int ref = lfm->ui.maps.cur->ref;
        lfm->ui.maps.cur = NULL;
        llua_call_from_ref(lfm->L, ref, -1);
      }
    } else {
      // definitely no mode map. if the character is not printable, check for
      // input maps
      if (!iswprint(in) && lfm->ui.maps.cur_input == NULL) {
        lfm->ui.maps.cur_input = lfm->ui.maps.input;
      }
      // if input map trie is active, check for a map even if the key is
      // printable
      if (lfm->ui.maps.cur_input != NULL) {
        lfm->ui.maps.cur_input = trie_find_child(lfm->ui.maps.cur_input, in);
        if (lfm->ui.maps.cur_input) {
          // current key sequence is a prefix/full match of a mode mapping,
          // always taking precedence
          if (lfm->ui.maps.cur_input->ref) {
            int ref = lfm->ui.maps.cur_input->ref;
            lfm->ui.maps.cur_input = NULL;
            llua_call_from_ref(lfm->L, ref, -1);
          } else {
            // map still possible, we might even show the mappings on screen
          }
        }
      } else if (iswprint(in)) {
        char buf[MB_LEN_MAX + 1];
        int n = wctomb(buf, in);
        if (n < 0) {
          log_error("invalid input: %lu", in);
          n = 0;
        }
        buf[n] = '\0';
        if (cmdline_insert(&ui->cmdline, zsview_from_n(buf, n))) {
          ui_redraw(ui, REDRAW_CMDLINE);
        }
        mode_on_change(lfm->current_mode, lfm);
      }
    }
  } else {
    // non-input mode, printable keys are mappings
    if (!lfm->ui.maps.cur) {
      lfm->ui.maps.cur = lfm->current_mode->maps;
      vec_input_clear(&lfm->ui.maps.seq);
      lfm->ui.maps.count = -1;
      lfm->ui.maps.accept_count = true;
    }
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
    if (in == NCKEY_ESC) {
      uint32_t mode = 0;
      if (!vec_input_is_empty(&lfm->ui.maps.seq)) {
        input_clear(lfm);
      } else {
        if (fm_selection_clear(&lfm->fm)) {
          mode |= REDRAW_FM;
        }
        if (fm_paste_buffer_clear(fm)) {
          lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
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
    } else if (!lfm->ui.maps.cur) {
      vec_input_push(&lfm->ui.maps.seq, in);
      char buf[256];
      size_t len;
      int j = 0;
      c_foreach(it, vec_input, lfm->ui.maps.seq) {
        const char *str = input_to_key_name(*it.ref, &len);
        if (j + len > sizeof buf - 1) {
          break;
        }
        memcpy(buf + j, str, len);
        j += len;
      }
      buf[j] = 0;
      log_debug("unmapped key sequence: %s (id=%d shift=%d ctrl=%d alt=%d)",
                buf, ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in));
      input_clear(lfm);
    } else if (lfm->ui.maps.cur->is_leaf) {
      // A command is mapped to the current keysequence. Execute it and reset.
      int ref = lfm->ui.maps.cur->ref;
      input_clear(lfm);
      uint64_t t0 = current_micros();
      llua_call_from_ref(lfm->L, ref, lfm->ui.maps.count);
      uint64_t t1 = current_micros();
      log_trace("llua_call_from_ref %luus", t1 - t0);
    } else {
      vec_input_push(&lfm->ui.maps.seq, in);
      ui_redraw(ui, REDRAW_CMDLINE);
      lfm->ui.maps.accept_count = false;

      lfm->ui.map_clear_timer.repeat = (float)cfg.map_clear_delay / 1000.0;
      ev_timer_again(lfm->loop, &lfm->ui.map_clear_timer);

      lfm->ui.map_suggestion_timer.repeat =
          (float)cfg.map_suggestion_delay / 1000.0;
      ev_timer_again(lfm->loop, &lfm->ui.map_suggestion_timer);
    }
  }
}

static void map_clear_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
  Lfm *lfm = w->data;
  input_clear(lfm);
  ev_timer_stop(EV_A_ w);
  ev_idle_start(EV_A_ & lfm->ui.redraw_watcher);
}

static void map_suggestion_timer_cb(EV_P_ ev_timer *w, int revents) {
  (void)revents;
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
  ev_timer_stop(EV_A_ w);
}
