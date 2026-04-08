#include "visual.h"

#include "hooks.h"
#include "lfm.h"
#include "selection.h"

struct mode visual_mode;

static void visual_on_enter(struct Lfm *lfm);
static void visual_on_exit(struct Lfm *lfm);

void __attribute__((constructor)) init() {
  visual_mode = (struct mode){
      .name = cstr_lit("visual"),
      .is_input = false,
      .type = MODE_BUILTIN,
      .on_enter = visual_on_enter,
      .on_exit = visual_on_exit,
  };
}

void visual_update_selection(Fm *fm, u32 from, u32 to) {
  if (!fm->visual.active)
    return;
  u32 origin = fm->visual.anchor;
  u32 hi, lo;
  if (from >= origin) {
    if (to > from) {
      lo = from + 1;
      hi = to;
    } else if (to < origin) {
      hi = from;
      lo = to;
    } else {
      hi = from;
      lo = to + 1;
    }
  } else {
    if (to < from) {
      lo = to;
      hi = from - 1;
    } else if (to > origin) {
      lo = from;
      hi = to;
    } else {
      lo = from;
      hi = to - 1;
    }
  }
  const Dir *dir = fm_current_dir(fm);
  for (; lo <= hi; lo++) {
    // never unselect the old selection
    zsview path = file_path(*vec_file_at(&dir->files, lo));
    if (!pathlist_contains(&fm->selection.keep_in_visual, path)) {
      selection_toggle_path(fm, path, false);
    }
  }
  LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
}

static void visual_on_enter(Lfm *lfm) {
  Fm *fm = &lfm->fm;
  if (fm->visual.active)
    return;

  Dir *dir = fm_current_dir(fm);
  if (dir_length(dir) == 0)
    return;

  fm->visual.active = true;
  fm->visual.anchor = dir->ind;

  selection_add_path(fm, file_path(dir_current_file(dir)), false);
  pathlist_clear(&fm->selection.keep_in_visual);
  c_foreach(it, pathlist, fm->selection.current) {
    pathlist_add(&fm->selection.keep_in_visual, cstr_zv(it.ref));
  }
  LFM_RUN_HOOK(lfm_instance(), LFM_HOOK_SELECTION);
  ui_redraw(&lfm->ui, REDRAW_FM);
}

static void visual_on_exit(Lfm *lfm) {
  Fm *fm = &lfm->fm;
  if (!fm->visual.active)
    return;

  fm->visual.active = false;
  fm->visual.anchor = 0;
  pathlist_clear(&fm->selection.keep_in_visual);
}
