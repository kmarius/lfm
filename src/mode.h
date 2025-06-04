#pragma once

#include "trie.h"

#include "stc/cstr.h"
#include "stc/zsview.h"

#include <stdbool.h>

struct Lfm;

struct mode {
  cstr name;                       // name of the mode
  bool is_input;                   // capture command line input
  cstr prefix;                     // prefix to show in case is_input is true
  int on_enter_ref;                // lua ref to on_enter function
  int on_change_ref;               // lua ref to on_change function
  int on_return_ref;               // lua ref to on_return function
  int on_esc_ref;                  // lua ref to on_escape function
  int on_exit_ref;                 // lua ref to on_exit function
  void (*on_enter)(struct Lfm *);  // pointer to on_enter function
  void (*on_change)(struct Lfm *); // pointer to on_change function
  void (*on_return)(struct Lfm *,
                    zsview);     // pointer to on_return function
  void (*on_esc)(struct Lfm *);  // pointer to on_esc function
  void (*on_exit)(struct Lfm *); // pointer to on_exit function
  Trie *maps;
};

static inline void mode_drop(struct mode *mode) {
  trie_destroy(mode->maps);
  cstr_drop(&mode->name);
  cstr_drop(&mode->prefix);
}

static inline struct mode mode_valfrom(struct mode mode) {
  mode.name = cstr_clone(mode.name);
  mode.prefix =
      cstr_is_empty(&mode.prefix) ? cstr_init() : cstr_clone(mode.prefix);
  mode.maps = trie_create();
  return mode;
}

#define i_type hmap_modes, cstr, struct mode
#define i_keyraw zsview
#define i_keytoraw cstr_zv
#define i_keyfrom cstr_from_zv
#define i_valdrop mode_drop
#define i_valfrom mode_valfrom
#define i_hash zsview_hash
#define i_eq zsview_eq
#define i_no_clone
#include "stc/hmap.h"

/*
 * Initialize mode functionality.
 */
void lfm_modes_init(struct Lfm *lfm);

/*
 * Deinitialize mode functionality.
 */
void lfm_modes_deinit(struct Lfm *lfm);

/*
 * Register a new mode. Returns 1 if a mode with the same name already exists, 0
 * otherwise.
 */
int lfm_mode_register(struct Lfm *lfm, struct mode *mode);

/*
 * Enter the mode with the name `name`. Calls necessary callbacks/hooks
 * and cleans/sets up the command line.
 * Returns 1 if the mode does not exist, 0 otherwise.
 */
int lfm_mode_enter(struct Lfm *lfm, zsview name);

/*
 * Enter normal mode.
 */
static inline int lfm_mode_normal(struct Lfm *lfm) {
  return lfm_mode_enter(lfm, c_zv("normal"));
}

/*
 * Exit the mode (i.e. enters "normal" mode) with the name `name` if it is
 * the current mode. Returns 1 if a different mode is active.
 */
int lfm_mode_exit(struct Lfm *lfm, zsview name);

/*
 * Call on_enter callback for `mode`.
 */
void mode_on_enter(struct mode *mode, struct Lfm *lfm);

/*
 * Call on_change callback for `mode`.
 */
void mode_on_change(struct mode *mode, struct Lfm *lfm);

/*
 * Call on_return callback with command `line` for `mode`.
 */
void mode_on_return(struct mode *mode, struct Lfm *lfm, zsview line);

/*
 * Call on_exit callback for `mode`.
 */
void mode_on_exit(struct mode *mode, struct Lfm *lfm);

/*
 * Call on_escape callback for `mode`.
 */
void mode_on_esc(struct mode *mode, struct Lfm *lfm);
