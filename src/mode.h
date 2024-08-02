#pragma once

#include "trie.h"

#include <stdbool.h>

struct Lfm;

struct mode {
  char *name;                      // name of the mode
  bool input;                      // capture command line input
  char *prefix;                    // prefix to show in case input is set
  int on_enter_ref;                // lua ref to on_enter function
  int on_change_ref;               // lua ref to on_change function
  int on_return_ref;               // lua ref to on_return function
  int on_esc_ref;                  // lua ref to on_escape function
  int on_exit_ref;                 // lua ref to on_exit function
  void (*on_enter)(struct Lfm *);  // pointer to on_enter function
  void (*on_change)(struct Lfm *); // pointer to on_change function
  void (*on_return)(struct Lfm *,
                    const char *); // pointer to on_return function
  void (*on_esc)(struct Lfm *);    // pointer to on_esc function
  void (*on_exit)(struct Lfm *);   // pointer to on_exit function
  Trie *maps;
};

static inline void mode_drop(struct mode *mode) {
  trie_destroy(mode->maps);
  free(mode->name);
  free(mode->prefix);
}

static inline struct mode mode_valfrom(struct mode mode) {
  mode.name = strdup(mode.name);
  mode.prefix = mode.prefix ? strdup(mode.prefix) : NULL;
  mode.maps = trie_create();
  return mode;
}

#define i_type hmap_modes
#define i_key const char *
#define i_val struct mode
#define i_no_clone
#define i_valdrop mode_drop
#define i_valfrom mode_valfrom
#define i_hash ccharptr_hash
#define i_eq(p, q) (!strcmp(*(p), *(q)))
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
int lfm_mode_register(struct Lfm *lfm, const struct mode *mode);

/*
 * Enter the mode with the name `name`. Calls necessary callbacks/hooks
 * and cleans/sets up the command line.
 * Returns 1 if the mode does not exist, 0 otherwise.
 */
int lfm_mode_enter(struct Lfm *lfm, const char *name);

/*
 * Exit the mode (i.e. enters "normal" mode) with the name `name` if it is
 * the current mode. Returns 1 if a different mode is active.
 */
int lfm_mode_exit(struct Lfm *lfm, const char *name);

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
void mode_on_return(struct mode *mode, struct Lfm *lfm, const char *line);

/*
 * Call on_exit callback for `mode`.
 */
void mode_on_exit(struct mode *mode, struct Lfm *lfm);

/*
 * Call on_escape callback for `mode`.
 */
void mode_on_esc(struct mode *mode, struct Lfm *lfm);
