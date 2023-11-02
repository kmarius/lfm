#pragma once

#include "trie.h"
#include <stdbool.h>

struct lfm_s;

struct mode {
  char *name;                        // name of the mode
  bool input;                        // capture command line input
  char *prefix;                      // prefix to show in case input is set
  int on_enter_ref;                  // lua ref to on_enter function
  int on_change_ref;                 // lua ref to on_change function
  int on_return_ref;                 // lua ref to on_return function
  int on_esc_ref;                    // lua ref to on_escape function
  int on_exit_ref;                   // lua ref to on_exit function
  void (*on_enter)(struct lfm_s *);  // pointer to on_enter function
  void (*on_change)(struct lfm_s *); // pointer to on_change function
  void (*on_return)(struct lfm_s *,
                    const char *); // pointer to on_return function
  void (*on_esc)(struct lfm_s *);  // pointer to on_esc function
  void (*on_exit)(struct lfm_s *); // pointer to on_exit function
  Trie *maps;
};

void lfm_modes_init(struct lfm_s *lfm);

void lfm_modes_deinit(struct lfm_s *lfm);

int lfm_mode_register(struct lfm_s *lfm, const struct mode *mode);

int lfm_mode_enter(struct lfm_s *lfm, const char *name);

int lfm_mode_exit(struct lfm_s *lfm, const char *name);

void mode_on_enter(struct mode *mode, struct lfm_s *lfm);

void mode_on_change(struct mode *mode, struct lfm_s *lfm);

void mode_on_return(struct mode *mode, struct lfm_s *lfm, const char *line);

void mode_on_exit(struct mode *mode, struct lfm_s *lfm);

void mode_on_esc(struct mode *mode, struct lfm_s *lfm);
