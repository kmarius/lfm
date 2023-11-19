#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hashtab.h"
#include "util.h"

// Take the lowest l bits from hash value
#define HASH(h, l) ((h) & ((1 << (l)) - 1))

// https://en.m.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static inline uint64_t hash(const char *s) {
  uint64_t h = 0xcbf29ce484222325;
  while (*s) {
    h = (h ^ *s++) * 0x00000100000001B3;
  }
  return h;
}

static inline bool is_pow2(uint64_t n) {
  return __builtin_popcountl(n) == 1;
}

static inline uint64_t next_pow2(uint64_t n) {
  return 1 << (8 * (sizeof n) - __builtin_clzl(n));
}

Hashtab *ht_init(Hashtab *ht, size_t capacity, ht_free_func free) {
  if (!is_pow2(capacity)) {
    capacity = next_pow2(capacity);
  }
  memset(ht, 0, sizeof *ht);
  ht->capacity = capacity;
  ht->blvl = __builtin_ctzl(capacity);
  ht->xlvl = ht->blvl;
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
    size_t cap = 1 << (ht->xlvl + 1);
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
      uint64_t h = HASH(hash(list->key), ht->xlvl + 1);
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
    if (ht->xlvl == ht->blvl) {
      return;
    }
    ht->xptr = ht->capacity / 2;
    ht->xlvl--;
  }
  // (almost) always find nonempty bucket to shrink, or we wont shrink fast
  // enough
  while (ht->xptr > 1 && ht->buckets[ht->capacity - 1].key == NULL) {
    ht->xptr--;
    ht->capacity--;
  }
  ht->xptr--;
  ht->capacity--;

  struct ht_bucket *src_bucket = &ht->buckets[ht->capacity];

  if (src_bucket->key) {
    struct ht_bucket *tgt_bucket = &ht->buckets[ht->xptr];
    if (tgt_bucket->key) {
      struct ht_bucket *tmp = xmalloc(sizeof *tmp);
      *tmp = *src_bucket;
      struct ht_bucket *end = tgt_bucket;
      for (; end->next; end = end->next) {
      }
      end->next = tmp;
    } else {
      *tgt_bucket = *src_bucket;
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
  uint64_t h_next = HASH(hash(key), ht->xlvl + 1);
  uint64_t h_cur = HASH(h_next, ht->xlvl);
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
  if (ht->size > HT_GROW_THRESHOLD * ht->capacity) {
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
        struct ht_bucket *tmp = b->next;
        *b = *tmp;
        xfree(tmp);
      } else {
        memset(b, 0, sizeof *b);
      }
    }
    if (ht->size < HT_SHRINK_THRESHOLD * ht->capacity) {
      ht_shrink(ht);
    }
    return true;
  }
  return false;
}

void ht_clear(Hashtab *ht) {
  uint8_t l = ht->blvl;
  ht_free_func free = ht->free;
  ht_deinit(ht);
  ht_init(ht, 1 << l, free);
}

LinkedHashtab *lht_init(LinkedHashtab *ht, size_t capacity, ht_free_func free) {
  if (!is_pow2(capacity)) {
    capacity = next_pow2(capacity);
  }
  memset(ht, 0, sizeof *ht);
  ht->capacity = capacity;
  ht->blvl = __builtin_ctzl(capacity);
  ht->xlvl = ht->blvl;
  ht->buckets = xcalloc(ht->capacity, sizeof *ht->buckets);
  ht->free = free;
  return ht;
}

LinkedHashtab *lht_deinit(LinkedHashtab *ht) {
  for (size_t i = 0; i < ht->capacity; i++) {
    if (ht->buckets[i].key) {
      for (struct lht_bucket *next, *b = ht->buckets[i].next; b; b = next) {
        next = b->next;
        if (ht->free) {
          ht->free(b->val);
        }
        free(b);
      }
      if (ht->free) {
        ht->free(ht->buckets[i].val);
      }
    }
  }
  xfree(ht->buckets);
  return ht;
}

LinkedHashtab *lht_with_capacity(size_t capacity, ht_free_func free) {
  return lht_init(xmalloc(sizeof(LinkedHashtab)), capacity, free);
}

void lht_destroy(LinkedHashtab *ht) {
  xfree(lht_deinit(ht));
}

// realloc the buckets array and fix the linkage.
static void lht_realloc(LinkedHashtab *ht, size_t nmemb) {
  // This is needed because we using a realloc'd pointer afterwars is UB
  // and might get optimized away in some way
  const volatile uintptr_t old_ptr = (uintptr_t)ht->buckets;

  ht->buckets = xrealloc(ht->buckets, nmemb * sizeof *ht->buckets);
  struct lht_bucket *new = ht->buckets;
  const struct lht_bucket *old = (void *)old_ptr;
  if (old == new) {
    return;
  }
  const ptrdiff_t diff = (uintptr_t) new - (uintptr_t)old;

  // correct all pointers to array buckets
  if (ht->first >= old && ht->first < old + ht->capacity) {
    ht->first = (void *)((uintptr_t)ht->first + diff);
  }
  if (ht->last >= old && ht->last < old + ht->capacity) {
    ht->last = (void *)((uintptr_t)ht->last + diff);
  }
  for (size_t i = 0; i < ht->capacity; i++) {
    if (!new[i].key) {
      continue;
    }
    if (new[i].order_prev) {
      if (new[i].order_prev >= old &&new[i].order_prev < old + ht->capacity) {
        // array element
        new[i].order_prev = (void *)((ptrdiff_t) new[i].order_prev + diff);
      } else {
        // heap element
        new[i].order_prev->order_next = &new[i];
      }
    }
    if (new[i].order_next) {
      if (new[i].order_next >= old &&new[i].order_next < old + ht->capacity) {
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
static inline void lht_expand(LinkedHashtab *ht) {
  if (ht->xptr == 0) {
    // expand the bucket array
    size_t cap = 1 << (ht->xlvl + 1);
    lht_realloc(ht, cap);
    memset(ht->buckets + cap / 2, 0, cap / 2 * sizeof *ht->buckets);
  }

  struct lht_bucket *src_bucket = &ht->buckets[ht->xptr];
  // rehash current bucket
  if (src_bucket->key) {
    struct lht_bucket *tgt_bucket = &ht->buckets[ht->capacity];
    struct lht_bucket *keep_list_end = src_bucket;
    struct lht_bucket *tgt_list_end = tgt_bucket;

    // copy the array bucket to the heap for simplicity
    struct lht_bucket *list = xmalloc(sizeof *list);
    *list = *src_bucket;
    memset(src_bucket, 0, sizeof *src_bucket);

    FIX_LINKAGE(ht, list);

    for (; list; list = list->next) {
      uint64_t h = HASH(hash(list->key), ht->xlvl + 1);
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
      struct lht_bucket *tmp = src_bucket->next;
      *src_bucket = *tmp;
      free(tmp);
      FIX_LINKAGE(ht, src_bucket);
    }
    if (tgt_list_end != tgt_bucket) {
      struct lht_bucket *tmp = tgt_bucket->next;
      *tgt_bucket = *tmp;
      free(tmp);
      FIX_LINKAGE(ht, tgt_bucket);
    }
  }
  ht->xptr++;
  ht->capacity++;
  if (ht->xptr * 2 == ht->capacity) {
    ht->xptr = 0;
    ht->xlvl++;
  }
}

static inline void lht_shrink(LinkedHashtab *ht) {
  if (ht->xptr == 0) {
    if (ht->xlvl == ht->blvl) {
      return;
    }
    ht->xptr = ht->capacity / 2;
    ht->xlvl--;
  }
  // (almost) always find nonempty bucket to shrink, or we wont shrink fast
  // enough
  while (ht->xptr > 1 && ht->buckets[ht->capacity - 1].key == NULL) {
    ht->xptr--;
    ht->capacity--;
  }
  ht->xptr--;
  ht->capacity--;

  struct lht_bucket *src_bucket = &ht->buckets[ht->capacity];

  if (src_bucket->key) {
    struct lht_bucket *tgt_bucket = &ht->buckets[ht->xptr];
    if (tgt_bucket->key) {
      struct lht_bucket *tmp = xmalloc(sizeof *tmp);
      *tmp = *src_bucket;
      FIX_LINKAGE(ht, tmp);
      struct lht_bucket *end = tgt_bucket;
      for (; end->next; end = end->next) {
      }
      end->next = tmp;
    } else {
      *tgt_bucket = *src_bucket;
      FIX_LINKAGE(ht, tgt_bucket);
    }
    memset(src_bucket, 0, sizeof *src_bucket);
  }

  if (ht->xptr == 0) {
    // expansion completely undone
    lht_realloc(ht, ht->capacity);
  }
}

// Returns true on successfull lookup, sets *b to the corresponding bucket.
// On fail, b contains either the empty bucket in the bucket array OR the
// nonempty bucket to which the next bucket should be appended to.
// (distinguish with (*b)->val != NULL) if prev is non NULL, it will be set
// to the previous bucket of the overflow list, which is needed for deletion.
static bool lht_probe(const LinkedHashtab *ht, const char *key,
                      struct lht_bucket **b, struct lht_bucket **prev) {
  const uint64_t h_next = HASH(hash(key), ht->xlvl + 1);
  const uint64_t h_cur = HASH(h_next, ht->xlvl);
  *b = &ht->buckets[h_cur < ht->xptr ? h_next : h_cur];
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

bool lht_set(LinkedHashtab *ht, const char *key, void *val) {
  bool ret = false;
  struct lht_bucket *b;
  if (!lht_probe(ht, key, &b, NULL)) {
    ret = true;
    if (b->key) {
      b->next = xcalloc(1, sizeof *b->next);
      b = b->next;
    }
    b->next = NULL;
    b->order_next = NULL;
    if (ht->last) {
      ht->last->order_next = b;
      b->order_prev = ht->last;
    } else {
      b->order_prev = NULL;
    }
    ht->last = b;
    if (!ht->first) {
      ht->first = b;
    }
  } else if (b->key && ht->free) {
    ht->free(b->val);
  }
  if (!b->key) {
    ht->size++;
  }
  b->key = key;
  b->val = val;
  if (ht->size > HT_GROW_THRESHOLD * ht->capacity) {
    lht_expand(ht);
  }
  return ret;
}

bool lht_delete(LinkedHashtab *ht, const char *key) {
  struct lht_bucket *b, *prev;
  if (lht_probe(ht, key, &b, &prev) && b->key) {
    ht->size--;
    if (ht->free) {
      ht->free(b->val);
    }
    if (b->order_prev) {
      b->order_prev->order_next = b->order_next;
    } else {
      ht->first = b->order_next;
    }
    if (b->order_next) {
      b->order_next->order_prev = b->order_prev;
    } else {
      ht->last = b->order_prev;
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
        FIX_LINKAGE(ht, b);
      } else {
        memset(b, 0, sizeof *b);
      }
    }
    if (ht->size == 0) {
      ht->first = ht->last = NULL;
    }
    if (ht->size < HT_SHRINK_THRESHOLD * ht->capacity) {
      lht_shrink(ht);
    }
    return true;
  }
  return false;
}

void *lht_get(const LinkedHashtab *ht, const char *key) {
  struct lht_bucket *b;
  if (lht_probe(ht, key, &b, NULL)) {
    return b->val;
  }
  return NULL;
}

void lht_clear(LinkedHashtab *ht) {
  uint8_t l = ht->blvl;
  ht_free_func free = ht->free;
  lht_deinit(ht);
  lht_init(ht, 1 << l, free);
}
