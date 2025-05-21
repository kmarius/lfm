#include "statusline.h"

#include "config.h"
#include "file.h"
#include "keys.h"
#include "lfm.h"
#include "macros.h"
#include "macros_defs.h"
#include "ui.h"

#include <curses.h>

static inline char *my_strftime(time_t time, char *buffer, size_t bufsz);
static inline uint32_t uint32_num_digits(uint32_t n);

void statusline_draw(Ui *ui) {
  Fm *fm = &to_lfm(ui)->fm;

  char nums[16];
  char size[32];
  char mtime[32];

  struct ncplane *n = ui->planes.cmdline;
  ncplane_erase(n);
  ncplane_set_bg_default(n);
  ncplane_set_fg_default(n);

  uint32_t rhs_sz = 0;
  uint32_t lhs_sz = 0;
  ncplane_cursor_yx(n, 0, 0);

  const Dir *dir = fm->dirs.visible.data[0];
  if (dir) {
    const File *file = dir_current_file(dir);
    if (file) {
      if (file_error(file)) {
        lhs_sz = ncplane_printf(n, "error: %s", strerror(file_error(file)));
      } else {
        char buf[512];
        buf[0] = 0;
        if (file_islink(file)) {
          const char *link_target = cstr_str(file_link_target(file));
          if (cfg.linkchars_len > 0) {
            snprintf(buf, sizeof buf - 1, " %s %s", cfg.linkchars, link_target);
          } else {
            snprintf(buf, sizeof buf - 1, " %s", link_target);
          }
        }
        lhs_sz = ncplane_printf(
            n, "%s %2.ld %s %s %4s %s%s", file_perms(file), file_nlink(file),
            file_owner(file), file_group(file), file_size_readable(file, size),
            my_strftime(file_mtime(file), mtime, sizeof mtime), buf);
      }
    }

    rhs_sz = snprintf(nums, sizeof nums, "%u/%u",
                      dir->length > 0 ? dir->ind + 1 : 0, dir->length);
    ncplane_putstr_yx(n, 0, ui->x - rhs_sz, nums);

    // these are drawn right to left
    if (dir->filter) {
      rhs_sz += mbstowcs(NULL, filter_string(dir->filter).str, 0) + 2 + 1;
      ncplane_set_bg_palindex(n, COLOR_GREEN);
      ncplane_set_fg_palindex(n, COLOR_BLACK);
      ncplane_putchar_yx(n, 0, ui->x - rhs_sz, ' ');
      ncplane_putstr(n, filter_string(dir->filter).str);
      ncplane_putchar(n, ' ');
      ncplane_set_bg_default(n);
      ncplane_set_fg_default(n);
      ncplane_putchar(n, ' ');
    }
    size_t paste_size = pathlist_size(&fm->paste.buffer);
    if (paste_size > 0) {
      if (fm->paste.mode == PASTE_MODE_COPY) {
        ncplane_set_channels(n, cfg.colors.copy);
      } else {
        ncplane_set_channels(n, cfg.colors.delete);
      }

      size_t paste_size = pathlist_size(&fm->paste.buffer);
      rhs_sz += uint32_num_digits(paste_size) + 2 + 1;
      ncplane_printf_yx(n, 0, ui->x - rhs_sz, " %zu ", paste_size);
      ncplane_set_bg_default(n);
      ncplane_set_fg_default(n);
      ncplane_putchar(n, ' ');
    }
    size_t sel_size = pathlist_size(&fm->selection.current);
    if (sel_size > 0) {
      ncplane_set_channels(n, cfg.colors.selection);
      rhs_sz += uint32_num_digits(sel_size) + 2 + 1;
      ncplane_printf_yx(n, 0, ui->x - rhs_sz, " %zu ", sel_size);
      ncplane_set_bg_default(n);
      ncplane_set_fg_default(n);
      ncplane_putchar(n, ' ');
    }
    if (dir->last_loading_action > 0 &&
        current_millis() - dir->last_loading_action >=
            cfg.loading_indicator_delay) {
      rhs_sz += 10;
      ncplane_set_bg_palindex(n, 237);
      ncplane_set_fg_palindex(n, 255);
      ncplane_putstr_yx(n, 0, ui->x - rhs_sz, " loading ");
      ncplane_set_bg_default(n);
      ncplane_set_fg_default(n);
      ncplane_putchar(n, ' ');
    }
    if (macro_recording) {
      char buf[256];
      snprintf(buf, sizeof buf, "recording @%s",
               input_to_key_name(macro_identifier));
      rhs_sz += mbstowcs(NULL, buf, 0) + 1;
      ncplane_putstr_yx(n, 0, ui->x - rhs_sz, buf);
      ncplane_putchar(n, ' ');
    }
    if (vec_input_size(&ui->maps.seq) > 0) {
      // unlikely we get to print this much anyway
      char buf[256];
      int i = 0;
      c_foreach(it, vec_input, ui->maps.seq) {
        const char *s = input_to_key_name(*it.ref);
        size_t l = strlen(s);
        if (i + l + 1 > sizeof buf) {
          break;
        }
        strcpy(buf + i, s);
        i += l;
      }
      buf[i] = 0;
      rhs_sz += mbstowcs(NULL, buf, 0) + 1;
      ncplane_putstr_yx(n, 0, ui->x - rhs_sz, buf);
      ncplane_putchar(n, ' ');
    }
    if (lhs_sz + rhs_sz > ui->x) {
      ncplane_putwc_yx(n, 0, ui->x - rhs_sz - 2, cfg.truncatechar);
      ncplane_putchar(n, ' ');
    }
  }
}

// Returns the the buffer instead of the number of printed bytes.
static inline char *my_strftime(time_t time, char *buffer, size_t bufsz) {
  strftime(buffer, bufsz, "%Y-%m-%d %H:%M:%S", localtime(&time));
  return buffer;
}

static inline uint32_t uint32_num_digits(uint32_t n) {
  uint32_t i = 1;
  while (n >= 10) {
    i++;
    n /= 10;
  }
  return i;
}
