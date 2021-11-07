#include <notcurses/notcurses.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "cmdline.h"

#define T cmdline_t

#define VSTR_INIT(vec, c) \
	do { \
		(vec).str = malloc(sizeof(*(vec).str) * ((c) + 1)); \
		(vec).cap = c; \
		(vec).str[0] = 0; \
		(vec).len = 0; \
	} while (0)

#define ENSURE_CAPACITY(vec, sz) \
	do { \
		if ((vec).cap < sz) { \
			while ((vec).cap < sz) \
				(vec).cap *= 2; \
			(vec).str = realloc((vec).str, sizeof(*vec.str) * (vec).cap * 2 + 1); \
		} \
	} while (0)

#define ENSURE_SPACE(vec, sz) \
	ENSURE_CAPACITY(vec, (size_t) (vec).len + sz)

#define SHIFT_RIGHT(t, _sz) \
	do { \
		int sz = _sz; \
		ENSURE_SPACE((t)->right, sz); \
		memmove((t)->right.str + (sz), (t)->right.str, sizeof(wchar_t)*((t)->right.len+1)); \
		memcpy((t)->right.str, (t)->left.str + (t)->left.len - (sz), sizeof(wchar_t)*(sz)); \
		(t)->right.len += (sz); \
		(t)->left.len -= (sz); \
		(t)->left.str[(t)->left.len] = 0; \
	} while (0)

#define SHIFT_LEFT(t, _sz) \
	do { \
		int sz = _sz; \
		ENSURE_SPACE((t)->left, sz); \
		memcpy((t)->left.str + (t)->left.len, (t)->right.str, sizeof(wchar_t)*(sz)); \
		memmove((t)->right.str, (t)->right.str + (sz), sizeof(wchar_t)*((t)->right.len-(sz)+1)); \
		(t)->right.len -= (sz); \
		(t)->left.len += (sz); \
		(t)->left.str[(t)->left.len] = 0; \
	} while (0)

void cmdline_init(T *t)
{
	VSTR_INIT(t->prefix, 8);
	VSTR_INIT(t->left, 8);
	VSTR_INIT(t->right, 8);
	VSTR_INIT(t->buf, 8);
}

bool cmdline_prefix_set(T *t, const char *prefix)
{
	if (!prefix) {
		return 0;
	}
	const int l = strlen(prefix);
	ENSURE_CAPACITY(t->prefix, (size_t) l);
	strcpy(t->prefix.str, prefix);
	t->prefix.len = l;
	return 1;
}

const char *cmdline_prefix_get(T *t)
{
	return t->prefix.len == 0 ? NULL : t->prefix.str;
}

bool cmdline_insert(T *t, const char *key)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	ENSURE_SPACE(t->left, 1);
	mbtowc(t->left.str+t->left.len, key, strlen(key));
	t->left.len++;
	t->left.str[t->left.len] = 0;
	return 1;
}

bool cmdline_delete(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	t->left.str[t->left.len - 1] = 0;
	t->left.len--;
	return 1;
}

bool cmdline_delete_right(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0) {
		return 0;
	}
	memmove(t->right.str, t->right.str+1, sizeof(wchar_t)*t->right.len);
	t->right.len--;
	return 1;
}

bool cmdline_delete_word(T *t)
{
	int i;
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	i = t->left.len - 1;
	while (i > 0 && iswspace(t->left.str[i]))
		i--;
	while (i > 0 && t->left.str[i] == '/' && !iswspace(t->left.str[i-1]))
		i--;
	while (i > 0 && !(iswspace(t->left.str[i-1]) || t->left.str[i-1] == '/'))
		i--;
	t->left.len = i;
	t->left.str[i] = 0;
	return 1;
}

/* pass a ct argument to move over words? */
bool cmdline_left(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	SHIFT_RIGHT(t, 1);
	return 1;
}

bool cmdline_word_left(T *t)
{
	int i;
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	i = t->left.len;
	if (i > 0 && iswpunct(t->left.str[i-1]))
		i--;
	if (i > 0 && iswspace(t->left.str[i-1]))
		while (i > 0 && iswspace(t->left.str[i-1]))
			i--;
	else
		while (i > 0 && !(iswspace(t->left.str[i-1]) || iswpunct(t->left.str[i-1])))
			i--;
	SHIFT_RIGHT(t, t->left.len-i);
	return 1;
}

bool cmdline_word_right(T *t)
{
	int i;
	if (t->prefix.len == 0 || t->right.len == 0) {
		return 0;
	}
	i = 0;
	if (i < t->right.len && iswpunct(t->right.str[i]))
		i++;
	if (i < t->right.len && iswspace(t->right.str[i]))
		while (i < t->right.len && iswspace(t->right.str[i]))
			i++;
	else
		while (i < t->right.len && !(iswspace(t->right.str[i]) || iswpunct(t->right.str[i])))
			i++;
	SHIFT_LEFT(t, i);
	return 1;
}

bool cmdline_right(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0) {
		return 0;
	}
	SHIFT_LEFT(t, 1);
	return 1;
}

bool cmdline_home(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	SHIFT_RIGHT(t, t->left.len);
	return 1;
}

bool cmdline_end(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0) {
		return 0;
	}
	SHIFT_LEFT(t, t->right.len);
	return 1;
}

bool cmdline_clear(T *t)
{
	t->prefix.str[0] = 0;
	t->prefix.len = 0;
	t->left.str[0] = 0;
	t->left.len = 0;
	t->right.str[0] = 0;
	t->right.len = 0;
	return 1;
}

bool cmdline_set_whole(T *t, const char *prefix, const char *left, const char *right)
{
	int n;
	ENSURE_SPACE(t->left, strlen(left));
	ENSURE_SPACE(t->right, strlen(right));
	cmdline_prefix_set(t, prefix);
	n = mbstowcs(t->left.str, left, t->left.cap + 1);
	if (n == -1) {
		t->left.len = 0;
		t->left.str[0] = 0;
	} else {
		t->left.len = n;
	}
	n = mbstowcs(t->right.str, right, t->right.cap + 1);
	if (n == -1) {
		t->right.len = 0;
		t->right.str[0] = 0;
	} else {
		t->right.len = n;
	}
	return 1;
}

bool cmdline_set(T *t, const char *line)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	t->right.str[0] = 0;
	t->right.len = 0;
	ENSURE_SPACE(t->left, strlen(line));
	const int n = mbstowcs(t->left.str, line, t->left.cap + 1);
	if (n == -1) {
		t->left.len = 0;
		t->left.str[0] = 0;
	} else {
		t->left.len = n;
	}
	return 1;
}

const char *cmdline_get(T *t)
{
	t->buf.str[0] = 0;
	if (t->prefix.len != 0) {
		ENSURE_SPACE(t->buf, (size_t) (t->left.len + t->right.len) * MB_CUR_MAX);
		size_t n = wcstombs(t->buf.str, t->left.str, t->left.len * MB_CUR_MAX);
		if (n == (size_t) -1) {
			return "";
		}
		size_t m = wcstombs(t->buf.str + n, t->right.str, t->right.len * MB_CUR_MAX);
		if (m == (size_t) -1) {
			return "";
		}
		t->buf.str[n+m] = 0;
	}
	return t->buf.str;
}

int cmdline_print(T *t, struct ncplane *n)
{
	int ret = 0;
	ret += ncplane_putstr_yx(n, 0, 0, t->prefix.str);
	ret += ncplane_putwstr(n, t->left.str);
	ncplane_putwstr(n, t->right.str);
	return ret;
}

void cmdline_deinit(T *t) {
	free(t->prefix.str);
	free(t->left.str);
	free(t->right.str);
	free(t->buf.str);
}
