#include "lfm.h"
#include "lualfm.h"
#include "log.h"
#include "lua.h"
#include "search.h"
#include "trie.h"
#include "ui.h"
#include "fm.h"
#include "hooks.h"

void input_init(Lfm *lfm)
{
  lfm->maps.normal = trie_create();
  lfm->maps.cmd = trie_create();
  lfm->maps.seq = NULL;
}

void input_deinit(Lfm *lfm)
{
  trie_destroy(lfm->maps.normal);
  trie_destroy(lfm->maps.cmd);
  cvector_free(lfm->maps.seq);
}

void lfm_handle_key(Lfm *lfm, input_t in)
{
  Ui *ui = &lfm->ui;
  Fm *fm = &lfm->fm;

  if (in == CTRL('Q')) {
    lfm_quit(lfm);
    return;
  }

  const char *prefix = cmdline_prefix_get(&ui->cmdline);
  if (!lfm->maps.cur) {
    lfm->maps.cur = prefix ? lfm->maps.cmd : lfm->maps.normal;
    cvector_set_size(lfm->maps.seq, 0);
    lfm->maps.count = -1;
    lfm->maps.accept_count = true;
  }
  if (!prefix && lfm->maps.accept_count && '0' <= in && in <= '9') {
    if (lfm->maps.count < 0) {
      lfm->maps.count = in - '0';
    } else {
      lfm->maps.count = lfm->maps.count * 10 + in - '0';
    }
    if (lfm->maps.count > 0) {
      cvector_push_back(lfm->maps.seq, in);
      ui_keyseq_show(ui, lfm->maps.seq);
    }
    return;
  }
  lfm->maps.cur = trie_find_child(lfm->maps.cur, in);
  if (prefix) {
    if (!lfm->maps.cur) {
      if (iswprint(in)) {
        char buf[MB_LEN_MAX+1];
        int n = wctomb(buf, in);
        if (n < 0) {
          n = 0; // invalid character or borked shift/ctrl/alt
        }
        buf[n] = '\0';
        if (cmdline_insert(&ui->cmdline, buf)) {
          ui_redraw(ui, REDRAW_CMDLINE);
        }
      }
      lua_call_on_change(lfm->L, prefix);
    } else {
      if (lfm->maps.cur->ref) {
        int ref = lfm->maps.cur->ref;
        lfm->maps.cur = NULL;
        lua_call_from_ref(lfm->L, ref, -1);
      }
    }
  } else {
    // prefix == NULL, i.e. normal mode
    if (in == NCKEY_ESC) {
      if (cvector_size(lfm->maps.seq) > 0) {
        // clear keys in the buffer
        lfm->maps.cur = NULL;
        ui_menu_hide(ui);
        ui_keyseq_hide(ui);
      } else {
        // clear selection etc
        // TODO: this should be done properly with modes (on 2022-02-13)
        nohighlight(ui);
        fm_selection_visual_stop(fm);
        fm_selection_clear(fm);
        fm_paste_buffer_clear(fm);
        lfm_run_hook(lfm, LFM_HOOK_PASTEBUF);
      }
      ui->message = false;
      ui_redraw(ui, REDRAW_FM);
    } else if (!lfm->maps.cur) {
      // no keymapping, print an error
      cvector_push_back(lfm->maps.seq, in);
      char *str = NULL;
      for (size_t i = 0; i < cvector_size(lfm->maps.seq); i++) {
        for (const char *s = input_to_key_name(lfm->maps.seq[i]); *s; s++) {
          cvector_push_back(str, *s);
        }
      }
      cvector_push_back(str, 0);
      log_debug("key: %d, id: %d, shift: %d, ctrl: %d alt %d, %s",
          in, ID(in), ISSHIFT(in), ISCTRL(in), ISALT(in), str);
      cvector_free(str);
      ui_menu_hide(ui);
      ui_keyseq_hide(ui);
    } else if (lfm->maps.cur->keys) {
      // A command is mapped to the current keysequence. Execute it and reset.
      ui_menu_hide(ui);
      int ref = lfm->maps.cur->ref;
      lfm->maps.cur = NULL;
      ui_keyseq_hide(ui);
      lua_call_from_ref(lfm->L, ref, lfm->maps.count);
    } else {
      cvector_push_back(lfm->maps.seq, in);
      ui_keyseq_show(ui, lfm->maps.seq);
      lfm->maps.accept_count = false;

      Trie **leaves = NULL;
      trie_collect_leaves(lfm->maps.cur, &leaves, true);

      char **menu = NULL;

      cvector_push_back(menu, strdup("\033[1mkeys\tcommand\033[0m"));
      char *s;
      for (size_t i = 0; i < cvector_size(leaves); i++) {
        asprintf(&s, "%s\t%s", leaves[i]->keys, leaves[i]->desc ? leaves[i]->desc : "");
        cvector_push_back(menu, s);
      }
      cvector_free(leaves);
      ui_menu_show(ui, menu);
    }
  }
}
