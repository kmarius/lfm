#include "file.h"
#include "sort.h"
#include "strnatcmp.h"

int compare_name(const void *a, const void *b)
{
  return strcasecmp(file_name(*(File **) a), file_name(*(File **) b));
}

int compare_size(const void *a, const void *b)
{
  const int64_t c = file_size(*(File **) a) - file_size(*(File **) b);
  return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int compare_natural(const void *a, const void *b)
{
  return strnatcasecmp(file_name(*(File **) a), file_name(*(File **) b));
}

int compare_ctime(const void *a, const void *b)
{
  return file_ctime(*(File **) b) - file_ctime(*(File **) a);
}

int compare_atime(const void *a, const void *b)
{
  return file_atime(*(File **) b) - file_atime(*(File **) a);
}

int compare_mtime(const void *a, const void *b)
{
  return file_mtime(*(File **) b) - file_mtime(*(File **) a);
}

// https://stackoverflow.com/questions/6127503/shuffle-array-in-c
// arrange the N elements of ARRAY in random order.
// Only effective if N is much smaller than RAND_MAX;
// if this may not be the case, use a better random
// number generator.
void shuffle(void *arr, size_t n, size_t size)
{
  if (n <= 1) {
    return;
  }

  const size_t stride = size;
  char *tmp = xmalloc(n * size);

  size_t i;
  for (i = 0; i < n - 1; ++i) {
    const size_t rnd = (size_t) rand();
    size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

    memcpy(tmp, arr + j * stride, size);
    memcpy(arr + j * stride, arr + i * stride, size);
    memcpy(arr + i * stride, tmp, size);
  }

  xfree(tmp);
}
