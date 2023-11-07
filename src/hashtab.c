#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hashtab.h"
#include "util.h"

// https://en.m.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static inline uint64_t hash(const char *s) {
  uint64_t h = 0xcbf29ce484222325;
  while (*s) {
    h = (h ^ *s++) * 0x00000100000001B3;
  }
  return h;
}

// Hash function to base n and level l. Calculate the larger hash first and
// reuse it, since for k < l, HASH(h, n, k) = HASH(HASH(h, n, l), n, k).
#define HASH(h, n, l) (h) % ((n) * (1 << (l)))

Hashtab *ht_init(Hashtab *ht, size_t capacity, ht_free_func free) {
  memset(ht, 0, sizeof *ht);
  ht->capacity = capacity;
  ht->n = capacity;
  ht->buckets = xcalloc(ht->capacity, sizeof *ht->buckets);
  ht->free = free;
  return ht;
}

Hashtab *ht_deinit(Hashtab *ht) {
  for (size_t i = 0; i < ht->capacity; i++) {
    if (ht->buckets[i].key) {
      for (struct ht_bucket *next, *b = ht->buckets[i].next; b; b = next) {
        next = b->next;
        if (ht->free) {
          ht->free(b->val);
        }
        xfree(b);
      }
      if (ht->free) {
        ht->free(ht->buckets[i].val);
      }
    }
  }
  xfree(ht->buckets);
  return ht;
}

Hashtab *ht_with_capacity(size_t capacity, ht_free_func free) {
  return ht_init(xmalloc(sizeof(Hashtab)), capacity, free);
}

void ht_destroy(Hashtab *ht) {
  xfree(ht_deinit(ht));
}

static inline void ht_realloc(Hashtab *ht, size_t nmemb) {
  ht->buckets = xrealloc(ht->buckets, nmemb * sizeof *ht->buckets);
}

// expand the current bucket+overflow list
static inline void ht_expand(Hashtab *ht) {
  if (ht->xptr == 0) {
    // expand the bucket array
    size_t cap = ht->n * (1 << (ht->xlvl + 1));
    ht_realloc(ht, cap);
    memset(ht->buckets + cap / 2, 0, cap / 2 * sizeof *ht->buckets);
  }

  struct ht_bucket *src_bucket = &ht->buckets[ht->xptr];
  // rehash current bucket
  if (src_bucket->key) {
    struct ht_bucket *tgt_bucket = &ht->buckets[ht->capacity];
    struct ht_bucket *keep_list_end = src_bucket;
    struct ht_bucket *tgt_list_end = tgt_bucket;

    // copy the array bucket to the heap for simplicity
    struct ht_bucket *list = xmalloc(sizeof *list);
    *list = *src_bucket;
    memset(src_bucket, 0, sizeof *src_bucket);

    for (; list; list = list->next) {
      uint64_t h = hash(list->key) % (ht->n * (2 << ht->xlvl));
      if (h != ht->xptr) {
        tgt_list_end->next = list;
        tgt_list_end = list;
      } else {
        keep_list_end->next = list;
        keep_list_end = list;
      }
    }

    keep_list_end->next = NULL;
    tgt_list_end->next = NULL;

    if (keep_list_end != src_bucket) {
      struct ht_bucket *tmp = src_bucket->next;
      *src_bucket = *tmp;
      xfree(tmp);
    }
    if (tgt_list_end != tgt_bucket) {
      struct ht_bucket *tmp = tgt_bucket->next;
      *tgt_bucket = *tmp;
      xfree(tmp);
    }
  }
  ht->xptr++;
  ht->capacity++;
  if (ht->xptr * 2 == ht->capacity) {
    ht->xptr = 0;
    ht->xlvl++;
  }
}

static inline void ht_shrink(Hashtab *ht) {
  if (ht->xptr == 0) {
    if (ht->xlvl == 0) {
      return;
    }
    ht->xptr = ht->capacity / 2;
    ht->xlvl--;
  }
  // (almost) always find nonempty bucket to shrink, or we wont shrink fast
  // enough
  while (ht->xptr > 2 && ht->buckets[ht->capacity - 1].key == NULL) {
    ht->xptr--;
    ht->capacity--;
  }
  ht->xptr--;
  ht->capacity--;

  struct ht_bucket *src_bucket = &ht->buckets[ht->capacity];

  if (src_bucket->key) {
    struct ht_bucket *tgt_bucket = &ht->buckets[ht->xptr];
    struct ht_bucket *list = xmalloc(sizeof *list);
    *list = *src_bucket;
    if (tgt_bucket->key) {
      struct ht_bucket *end = tgt_bucket;
      for (; end->next; end = end->next) {
      }
      end->next = list;
    } else {
      *tgt_bucket = *list;
      xfree(list);
    }
    memset(src_bucket, 0, sizeof *src_bucket);
  }

  // expansion pointer reached 0
  if (ht->xptr == 0) {
    ht_realloc(ht, ht->capacity);
  }
}

// Returns true if successfull and the corresponding bucket in b: Otherwise,
// b contains the bucket (head of the list) if empty, or the element to which
// the new node should be appended to (check with (*b)->val).
static bool ht_probe(Hashtab *ht, const char *key, struct ht_bucket **b,
                     struct ht_bucket **prev) {
  const uint64_t h_next = HASH(hash(key), ht->n, ht->xlvl + 1);
  const uint64_t h_cur = HASH(h_next, ht->n, ht->xlvl);
  *b = &ht->buckets[h_cur < ht->xptr ? h_next : h_cur];
  if (prev) {
    *prev = NULL;
  }
  for (;;) {
    struct ht_bucket *bb = *b;
    if (!bb->key) {
      return false;
    }
    if (streq(bb->key, key)) {
      return true;
    }
    if (!bb->next) {
      return false;
    }
    if (prev) {
      *prev = bb;
    }
    *b = bb->next;
  }
}

bool ht_set(Hashtab *ht, const char *key, void *val) {
  bool ret = false;
  struct ht_bucket *b;
  if (!ht_probe(ht, key, &b, NULL) && b->key) {
    ret = true;
    b->next = xcalloc(1, sizeof *b->next);
    b = b->next;
  } else if (b->key && ht->free) {
    ht->free(b->val);
  }
  if (!b->key) {
    ht->size++;
  }
  b->key = key;
  b->val = val;
  if (ht->size > HT_GROW_THRESHOLD * (ht->capacity + ht->xptr)) {
    ht_expand(ht);
  }
  return ret;
}

void *ht_get(Hashtab *ht, const char *key) {
  struct ht_bucket *b;
  if (ht_probe(ht, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}

bool ht_delete(Hashtab *ht, const char *key) {
  struct ht_bucket *b, *prev;
  if (ht_probe(ht, key, &b, &prev) && b->key) {
    ht->size--;
    if (ht->free) {
      ht->free(b->val);
    }
    if (prev) {
      // overflow bucket
      prev->next = b->next;
      xfree(b);
    } else {
      // array bucket
      if (b->next) {
        // move overflow bucket into array
        struct ht_bucket *next = b->next;
        *b = *next;
        xfree(next);
      } else {
        b->val = NULL;
        b->key = NULL;
      }
    }
    if (ht->size < HT_SHRINK_THRESHOLD * (ht->capacity + ht->xptr)) {
      ht_shrink(ht);
    }
    return true;
  }
  return false;
}

void ht_clear(Hashtab *ht) {
  const size_t n = ht->n;
  ht_free_func free = ht->free;
  ht_deinit(ht);
  ht_init(ht, n, free);
}

LinkedHashtab *lht_init(LinkedHashtab *lht, size_t capacity,
                        ht_free_func free) {
  memset(lht, 0, sizeof *lht);
  lht->capacity = capacity;
  lht->n = capacity;
  lht->buckets = xcalloc(lht->capacity, sizeof *lht->buckets);
  lht->free = free;
  return lht;
}

LinkedHashtab *lht_deinit(LinkedHashtab *lht) {
  for (size_t i = 0; i < lht->capacity; i++) {
    if (lht->buckets[i].key) {
      for (struct lht_bucket *next, *b = lht->buckets[i].next; b; b = next) {
        next = b->next;
        if (lht->free) {
          lht->free(b->val);
        }
        free(b);
      }
      if (lht->free) {
        lht->free(lht->buckets[i].val);
      }
    }
  }
  xfree(lht->buckets);
  return lht;
}

LinkedHashtab *lht_with_capacity(size_t capacity, ht_free_func free) {
  return lht_init(xmalloc(sizeof(LinkedHashtab)), capacity, free);
}

void lht_destroy(LinkedHashtab *lht) {
  xfree(lht_deinit(lht));
}

// realloc the buckets array and fix the linkage.
static void lht_realloc(LinkedHashtab *lht, size_t nmemb) {
  // THis is needed because we using a realloc'd pointer afterwars is UB
  // and might get optimized away in some way
  const volatile uintptr_t old_ptr = (uintptr_t)lht->buckets;
  lht->buckets = xrealloc(lht->buckets, nmemb * sizeof *lht->buckets);
  struct lht_bucket *new = lht->buckets;
  const struct lht_bucket *old = (void *)old_ptr;
  if (old == new) {
    return;
  }
  const ptrdiff_t diff = (uintptr_t) new - (uintptr_t)old;

  // correct all pointers to array buckets
  if (lht->first >= old && lht->first < old + lht->capacity) {
    lht->first = (void *)((uintptr_t)lht->first + diff);
  }
  if (lht->last >= old && lht->last < old + lht->capacity) {
    lht->last = (void *)((uintptr_t)lht->last + diff);
  }
  for (size_t i = 0; i < lht->capacity; i++) {
    if (!new[i].key) {
      continue;
    }
    if (new[i].order_prev) {
      if (new[i].order_prev >= old &&new[i].order_prev < old + lht->capacity) {
        // array element
        new[i].order_prev = (void *)((ptrdiff_t) new[i].order_prev + diff);
      } else {
        // heap element
        new[i].order_prev->order_next = &new[i];
      }
    }
    if (new[i].order_next) {
      if (new[i].order_next >= old &&new[i].order_next < old + lht->capacity) {
        // array element
        new[i].order_next = (void *)((ptrdiff_t) new[i].order_next + diff);
      } else {
        // heap element
        new[i].order_next->order_prev = &new[i];
      }
    }
  }
}

// fix linkage in predecessor and successor in insertion order when moving a
// bucket in or out of the array
#define FIX_LINKAGE(lht_, bucket_)                                             \
  do {                                                                         \
    if ((bucket_)->order_prev) {                                               \
      (bucket_)->order_prev->order_next = (bucket_);                           \
    } else {                                                                   \
      (lht_)->first = (bucket_);                                               \
    }                                                                          \
    if ((bucket_)->order_next) {                                               \
      (bucket_)->order_next->order_prev = (bucket_);                           \
    } else {                                                                   \
      (lht_)->last = (bucket_);                                                \
    }                                                                          \
  } while (0)

// expand the current bucket+overflow list
static inline void lht_expand(LinkedHashtab *lht) {
  if (lht->xptr == 0) {
    // expand the bucket array
    size_t cap = lht->n * (1 << (lht->xlvl + 1));
    lht_realloc(lht, cap);
    memset(lht->buckets + cap / 2, 0, cap / 2 * sizeof *lht->buckets);
  }

  struct lht_bucket *src_bucket = &lht->buckets[lht->xptr];
  // rehash current bucket
  if (src_bucket->key) {
    struct lht_bucket *tgt_bucket = &lht->buckets[lht->capacity];
    struct lht_bucket *keep_list_end = src_bucket;
    struct lht_bucket *tgt_list_end = tgt_bucket;

    // copy the array bucket to the heap for simplicity
    struct lht_bucket *list = xmalloc(sizeof *list);
    *list = *src_bucket;
    memset(src_bucket, 0, sizeof *src_bucket);

    FIX_LINKAGE(lht, list);

    for (; list; list = list->next) {
      uint64_t h = hash(list->key) % (lht->n * (2 << lht->xlvl));
      if (h != lht->xptr) {
        tgt_list_end->next = list;
        tgt_list_end = list;
      } else {
        keep_list_end->next = list;
        keep_list_end = list;
      }
    }

    keep_list_end->next = NULL;
    tgt_list_end->next = NULL;

    if (keep_list_end != src_bucket) {
      struct lht_bucket *tmp = src_bucket->next;
      *src_bucket = *tmp;
      free(tmp);
      FIX_LINKAGE(lht, src_bucket);
    }
    if (tgt_list_end != tgt_bucket) {
      struct lht_bucket *tmp = tgt_bucket->next;
      *tgt_bucket = *tmp;
      free(tmp);
      FIX_LINKAGE(lht, tgt_bucket);
    }
  }
  lht->xptr++;
  lht->capacity++;
  if (lht->xptr * 2 == lht->capacity) {
    lht->xptr = 0;
    lht->xlvl++;
  }
}

static inline void lht_shrink(LinkedHashtab *lht) {
  if (lht->xptr == 0) {
    if (lht->xlvl == 0) {
      return;
    }
    lht->xptr = lht->capacity / 2;
    lht->xlvl--;
  }
  // (almost) always find nonempty bucket to shrink, or we wont shrink fast
  // enough
  while (lht->xptr > 1 && lht->buckets[lht->capacity - 1].key == NULL) {
    lht->xptr--;
    lht->capacity--;
  }
  lht->xptr--;
  lht->capacity--;

  struct lht_bucket *src_bucket = &lht->buckets[lht->capacity];

  if (src_bucket->key) {
    struct lht_bucket *tgt_bucket = &lht->buckets[lht->xptr];
    if (tgt_bucket->key) {
      struct lht_bucket *tmp = xmalloc(sizeof *tmp);
      *tmp = *src_bucket;
      FIX_LINKAGE(lht, tmp);
      struct lht_bucket *end = tgt_bucket;
      for (; end->next; end = end->next) {
      }
      end->next = tmp;
    } else {
      *tgt_bucket = *src_bucket;
      FIX_LINKAGE(lht, tgt_bucket);
    }
  }

  if (lht->xptr == 0) {
    // expansion completely undone
    lht_realloc(lht, lht->capacity);
  }
}

// Returns true on successfull lookup, sets *b to the corresponding bucket.
// On fail, b contains either the empty bucket in the bucket array OR the
// nonempty bucket to which the next bucket should be appended to.
// (distinguish with (*b)->val != NULL) if prev is non NULL, it will be set
// to the previous bucket of the overflow list, which is needed for deletion.
static bool lht_probe(const LinkedHashtab *lht, const char *key,
                      struct lht_bucket **b, struct lht_bucket **prev) {
  const uint64_t h_next = HASH(hash(key), lht->n, lht->xlvl + 1);
  const uint64_t h_cur = HASH(h_next, lht->n, lht->xlvl);
  *b = &lht->buckets[h_cur < lht->xptr ? h_next : h_cur];
  if (prev) {
    *prev = NULL;
  }
  for (;;) {
    struct lht_bucket *bb = *b;
    if (!bb->key) {
      return false;
    }
    if (streq(bb->key, key)) {
      return true;
    }
    if (!bb->next) {
      return false;
    }
    if (prev) {
      *prev = bb;
    }
    *b = bb->next;
  }
}

bool lht_set(LinkedHashtab *lht, const char *key, void *val) {
  bool ret = false;
  struct lht_bucket *b;
  if (!lht_probe(lht, key, &b, NULL)) {
    ret = true;
    if (b->key) {
      b->next = xcalloc(1, sizeof *b->next);
      b = b->next;
    }
    b->next = NULL;
    b->order_next = NULL;
    if (lht->last) {
      lht->last->order_next = b;
      b->order_prev = lht->last;
    } else {
      b->order_prev = NULL;
    }
    lht->last = b;
    if (!lht->first) {
      lht->first = b;
    }
  } else if (b->key && lht->free) {
    lht->free(b->val);
  }
  if (!b->key) {
    lht->size++;
  }
  b->key = key;
  b->val = val;
  if (lht->size > HT_GROW_THRESHOLD * lht->capacity) {
    lht_expand(lht);
  }
  return ret;
}

bool lht_delete(LinkedHashtab *lht, const char *key) {
  struct lht_bucket *b, *prev;
  if (lht_probe(lht, key, &b, &prev) && b->key) {
    lht->size--;
    if (lht->free) {
      lht->free(b->val);
    }
    if (b->order_prev) {
      b->order_prev->order_next = b->order_next;
    } else {
      lht->first = b->order_next;
    }
    if (b->order_next) {
      b->order_next->order_prev = b->order_prev;
    } else {
      lht->last = b->order_prev;
    }
    if (prev) {
      // overflow bucket
      prev->next = b->next;
      xfree(b);
    } else {
      // array bucket
      if (b->next) {
        // move overflow bucket into array
        struct lht_bucket *tmp = b->next;
        *b = *b->next;
        xfree(tmp);
        FIX_LINKAGE(lht, b);
      } else {
        memset(b, 0, sizeof *b);
      }
    }
    if (lht->size == 0) {
      lht->first = lht->last = NULL;
    }
    if (lht->size < HT_SHRINK_THRESHOLD * lht->capacity) {
      lht_shrink(lht);
    }
    return true;
  }
  return false;
}

void *lht_get(const LinkedHashtab *lht, const char *key) {
  struct lht_bucket *b;
  if (lht_probe(lht, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}

void lht_clear(LinkedHashtab *lht) {
  size_t n = lht->n;
  ht_free_func free = lht->free;
  lht_deinit(lht);
  lht_init(lht, n, free);
}
