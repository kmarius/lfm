/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 John Hawthorn
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "fuzzy.h"
#include "memory.h"

#define SCORE_GAP_LEADING -0.005
#define SCORE_GAP_TRAILING -0.005
#define SCORE_GAP_INNER -0.01
#define SCORE_MATCH_CONSECUTIVE 1.0
#define SCORE_MATCH_SLASH 0.9
#define SCORE_MATCH_WORD 0.8
#define SCORE_MATCH_CAPITAL 0.7
#define SCORE_MATCH_DOT 0.6

#define ASSIGN_LOWER(v)                                                        \
  ['a'] = (v), ['b'] = (v), ['c'] = (v), ['d'] = (v), ['e'] = (v),             \
  ['f'] = (v), ['g'] = (v), ['h'] = (v), ['i'] = (v), ['j'] = (v),             \
  ['k'] = (v), ['l'] = (v), ['m'] = (v), ['n'] = (v), ['o'] = (v),             \
  ['p'] = (v), ['q'] = (v), ['r'] = (v), ['s'] = (v), ['t'] = (v),             \
  ['u'] = (v), ['v'] = (v), ['w'] = (v), ['x'] = (v), ['y'] = (v), ['z'] = (v)

#define ASSIGN_UPPER(v)                                                        \
  ['A'] = (v), ['B'] = (v), ['C'] = (v), ['D'] = (v), ['E'] = (v),             \
  ['F'] = (v), ['G'] = (v), ['H'] = (v), ['I'] = (v), ['J'] = (v),             \
  ['K'] = (v), ['L'] = (v), ['M'] = (v), ['N'] = (v), ['O'] = (v),             \
  ['P'] = (v), ['Q'] = (v), ['R'] = (v), ['S'] = (v), ['T'] = (v),             \
  ['U'] = (v), ['V'] = (v), ['W'] = (v), ['X'] = (v), ['Y'] = (v), ['Z'] = (v)

#define ASSIGN_DIGIT(v)                                                        \
  ['0'] = (v), ['1'] = (v), ['2'] = (v), ['3'] = (v), ['4'] = (v),             \
  ['5'] = (v), ['6'] = (v), ['7'] = (v), ['8'] = (v), ['9'] = (v)

const score_t bonus_states[3][256] = {
    {0},
    {
        ['/'] = SCORE_MATCH_SLASH,
        ['-'] = SCORE_MATCH_WORD,
        ['_'] = SCORE_MATCH_WORD,
        [' '] = SCORE_MATCH_WORD,
        ['.'] = SCORE_MATCH_DOT,
    },
    {['/'] = SCORE_MATCH_SLASH,
     ['-'] = SCORE_MATCH_WORD,
     ['_'] = SCORE_MATCH_WORD,
     [' '] = SCORE_MATCH_WORD,
     ['.'] = SCORE_MATCH_DOT,

     /* ['a' ... 'z'] = SCORE_MATCH_CAPITAL, */
     ASSIGN_LOWER(SCORE_MATCH_CAPITAL)}};

const size_t bonus_index[256] = {
    /* ['A' ... 'Z'] = 2 */
    ASSIGN_UPPER(2),

    /* ['a' ... 'z'] = 1 */
    ASSIGN_LOWER(1),

    /* ['0' ... '9'] = 1 */
    ASSIGN_DIGIT(1)};

#define COMPUTE_BONUS(last_ch, ch)                                             \
  (bonus_states[bonus_index[(unsigned char)(ch)]][(unsigned char)(last_ch)])

static inline char *strcasechr(const char *s, char c) {
  const char accept[3] = {c, toupper(c), 0};
  return strpbrk(s, accept);
}

int has_match(const char *needle, const char *haystack) {
  while (*needle) {
    char nch = *needle++;

    if (!(haystack = strcasechr(haystack, nch))) {
      return 0;
    }
    haystack++;
  }
  return 1;
}

#define SWAP(x, y, T)                                                          \
  do {                                                                         \
    T SWAP = x;                                                                \
    x = y;                                                                     \
    y = SWAP;                                                                  \
  } while (0)

#define max(a, b) (((a) > (b)) ? (a) : (b))

struct match_struct {
  int needle_len;
  int haystack_len;

  char lower_needle[MATCH_MAX_LEN];
  char lower_haystack[MATCH_MAX_LEN];

  score_t match_bonus[MATCH_MAX_LEN];
};

static inline void precompute_bonus(const char *haystack,
                                    score_t *match_bonus) {
  /* Which positions are beginning of words */
  char last_ch = '/';
  for (int i = 0; haystack[i]; i++) {
    char ch = haystack[i];
    match_bonus[i] = COMPUTE_BONUS(last_ch, ch);
    last_ch = ch;
  }
}

static inline void setup_match_struct(struct match_struct *match,
                                      const char *needle,
                                      const char *haystack) {
  match->needle_len = strlen(needle);
  match->haystack_len = strlen(haystack);

  if (match->haystack_len > MATCH_MAX_LEN ||
      match->needle_len > match->haystack_len) {
    return;
  }

  for (int i = 0; i < match->needle_len; i++)
    match->lower_needle[i] = tolower(needle[i]);

  for (int i = 0; i < match->haystack_len; i++)
    match->lower_haystack[i] = tolower(haystack[i]);

  precompute_bonus(haystack, match->match_bonus);
}

static inline void match_row(const struct match_struct *match, int row,
                             score_t *curr_D, score_t *curr_M,
                             const score_t *last_D, const score_t *last_M) {
  int n = match->needle_len;
  int m = match->haystack_len;
  int i = row;

  const char *lower_needle = match->lower_needle;
  const char *lower_haystack = match->lower_haystack;
  const score_t *match_bonus = match->match_bonus;

  score_t prev_score = SCORE_MIN;
  score_t gap_score = i == n - 1 ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

  for (int j = 0; j < m; j++) {
    if (lower_needle[i] == lower_haystack[j]) {
      score_t score = SCORE_MIN;
      if (!i) {
        score = (j * SCORE_GAP_LEADING) + match_bonus[j];
      } else if (j) { /* i > 0 && j > 0*/
        score = max(last_M[j - 1] + match_bonus[j],

                    /* consecutive match, doesn't stack with match_bonus */
                    last_D[j - 1] + SCORE_MATCH_CONSECUTIVE);
      }
      curr_D[j] = score;
      curr_M[j] = prev_score = max(score, prev_score + gap_score);
    } else {
      curr_D[j] = SCORE_MIN;
      curr_M[j] = prev_score = prev_score + gap_score;
    }
  }
}

score_t match(const char *needle, const char *haystack) {
  if (!*needle)
    return SCORE_MIN;

  struct match_struct match;
  setup_match_struct(&match, needle, haystack);

  int n = match.needle_len;
  int m = match.haystack_len;

  if (m > MATCH_MAX_LEN || n > m) {
    /*
     * Unreasonably large candidate: return no score
     * If it is a valid match it will still be returned, it will
     * just be ranked below any reasonably sized candidates
     */
    return SCORE_MIN;
  } else if (n == m) {
    /* Since this method can only be called with a haystack which
     * matches needle. If the lengths of the strings are equal the
     * strings themselves must also be equal (ignoring case).
     */
    return SCORE_MAX;
  }

  /*
   * D[][] Stores the best score for this position ending with a match.
   * M[][] Stores the best possible score at this position.
   */
  score_t D[2][MATCH_MAX_LEN], M[2][MATCH_MAX_LEN];

  score_t *last_D, *last_M;
  score_t *curr_D, *curr_M;

  last_D = D[0];
  last_M = M[0];
  curr_D = D[1];
  curr_M = M[1];

  for (int i = 0; i < n; i++) {
    match_row(&match, i, curr_D, curr_M, last_D, last_M);

    SWAP(curr_D, last_D, score_t *);
    SWAP(curr_M, last_M, score_t *);
  }

  return last_M[m - 1];
}

score_t match_positions(const char *needle, const char *haystack,
                        size_t *positions) {
  if (!*needle)
    return SCORE_MIN;

  struct match_struct match;
  setup_match_struct(&match, needle, haystack);

  int n = match.needle_len;
  int m = match.haystack_len;

  if (m > MATCH_MAX_LEN || n > m) {
    /*
     * Unreasonably large candidate: return no score
     * If it is a valid match it will still be returned, it will
     * just be ranked below any reasonably sized candidates
     */
    return SCORE_MIN;
  } else if (n == m) {
    /* Since this method can only be called with a haystack which
     * matches needle. If the lengths of the strings are equal the
     * strings themselves must also be equal (ignoring case).
     */
    if (positions)
      for (int i = 0; i < n; i++)
        positions[i] = i;
    return SCORE_MAX;
  }

  /*
   * D[][] Stores the best score for this position ending with a match.
   * M[][] Stores the best possible score at this position.
   */
  score_t(*D)[MATCH_MAX_LEN], (*M)[MATCH_MAX_LEN];
  M = xmalloc(sizeof(score_t) * MATCH_MAX_LEN * n);
  D = xmalloc(sizeof(score_t) * MATCH_MAX_LEN * n);

  score_t *last_D, *last_M = last_D = NULL;
  score_t *curr_D, *curr_M;

  for (int i = 0; i < n; i++) {
    curr_D = &D[i][0];
    curr_M = &M[i][0];

    match_row(&match, i, curr_D, curr_M, last_D, last_M);

    last_D = curr_D;
    last_M = curr_M;
  }

  /* backtrace to find the positions of optimal matching */
  if (positions) {
    int match_required = 0;
    for (int i = n - 1, j = m - 1; i >= 0; i--) {
      for (; j >= 0; j--) {
        /*
         * There may be multiple paths which result in
         * the optimal weight.
         *
         * For simplicity, we will pick the first one
         * we encounter, the latest in the candidate
         * string.
         */
        if (D[i][j] != SCORE_MIN && (match_required || D[i][j] == M[i][j])) {
          /* If this score was determined using
           * SCORE_MATCH_CONSECUTIVE, the
           * previous character MUST be a match
           */
          match_required =
              i && j && M[i][j] == D[i - 1][j - 1] + SCORE_MATCH_CONSECUTIVE;
          positions[i] = j--;
          break;
        }
      }
    }
  }

  score_t result = M[n - 1][m - 1];

  xfree(M);
  xfree(D);

  return result;
}
