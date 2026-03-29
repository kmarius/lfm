#include "selection.h"

#include "dir.h"
#include "file.h"
#include "fm.h"
#include "hooks.h"
#include "lfm.h"
#include "stc/common.h"

void selection_toggle_path(Fm *fm, zsview path, bool run_hook) {
  if (!pathlist_remove(&fm->selection.current, path))
    selection_add_path(fm, path, false);
  if (run_hook)
    LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
}

void selection_add_path(Fm *fm, zsview path, bool run_hook) {
  if (pathlist_add(&fm->selection.current, path) && run_hook) {
    LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
  }
}

bool selection_clear(Fm *fm) {
  if (!pathlist_empty(&fm->selection.current)) {
    c_swap(&fm->selection.current, &fm->selection.previous);
    pathlist_clear(&fm->selection.current);
    LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
    return true;
  }
  return false;
}

void selection_reverse(Fm *fm, Dir *dir) {
  c_foreach(it, Dir, dir) {
    selection_toggle_path(fm, file_path(*it.ref), false);
  }
  LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
}

void selection_write(Fm *fm, zsview path) {
  if (path.size > PATH_MAX) {
    lfm_errorf(lfm_instance(), "path too long");
    return;
  }

  if (make_dirs(path, 755) != 0) {
    lfm_perror(lfm_instance(), "mkdir");
    return;
  }

  FILE *fp = fopen(path.str, "w");
  if (!fp) {
    lfm_perror(lfm_instance(), "fopen");
    return;
  }

  if (!pathlist_empty(&fm->selection.current)) {
    c_foreach(it, pathlist, fm->selection.current) {
      fwrite(cstr_str(it.ref), 1, cstr_size(it.ref), fp);
      fputc('\n', fp);
    }
  } else {
    File *file = fm_current_file(fm);
    if (file) {
      fputs(file_path_str(file), fp);
      fputc('\n', fp);
    }
  }
  fclose(fp);
}

void paste_mode_set(Fm *fm, paste_mode mode) {
  fm->paste.mode = mode;
  if (pathlist_empty(&fm->selection.current)) {
    File *file = fm_current_file(fm);
    if (file)
      selection_toggle_path(fm, file_path(file), true);
  }
  pathlist_clear(&fm->paste.buffer);
  c_swap(&fm->paste.buffer, &fm->selection.current);
}

paste_mode paste_mode_get(const struct Fm *fm) {
  return fm->paste.mode;
}

bool paste_buffer_clear(Fm *fm) {
  usize prev_size = pathlist_size(&fm->paste.buffer);
  if (prev_size > 0) {
    // swap paste_buffer and prev
    pathlist tmp = fm->selection.previous;
    fm->selection.previous = fm->paste.buffer;
    fm->paste.buffer = tmp;
    pathlist_clear(&fm->paste.buffer);
  }
  return prev_size;
}

void paste_buffer_add(struct Fm *fm, zsview path) {
  pathlist_add(&fm->paste.buffer, path);
}
