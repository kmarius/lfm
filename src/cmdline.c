#include <notcurses/notcurses.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "cmdline.h"

#define T cmdline_t

/* TODO: clean up macros, create shift macros etc. (on 2021-10-25) */

#define vstr_init(vec, c) \
	do { \
		(vec).str = malloc(sizeof(*(vec).str) * ((c) + 1)); \
		(vec).cap = c; \
		(vec).str[0] = 0; \
		(vec).len = 0; \
	} while (0)

#define ensure_capacity(vec, sz) \
	do { \
		if ((vec).cap + 1 < sz) { \
			while ((vec).cap + 1 < sz) { \
				(vec).cap *= 2; \
			} \
			(vec).str = realloc((vec).str, sizeof(*vec.str) * (vec).cap * 2 + 1); \
		} \
	} while (0)

#define ensure_space(vec, sz) \
		ensure_capacity(vec, (size_t) (vec).len + sz)

void cmdline_init(cmdline_t *t)
{
	vstr_init(t->prefix, 8);
	vstr_init(t->left, 8);
	vstr_init(t->right, 8);
	vstr_init(t->buf, 8);
}

bool cmdline_prefix_set(T *t, const char *prefix)
{
	if (!prefix) {
		return 0;
	}
	const int l = strlen(prefix);
	ensure_capacity(t->prefix, (size_t) l);
	strcpy(t->prefix.str, prefix);
	t->prefix.len = l;
	return 1;
}

const char *cmdline_prefix_get(T *t)
{
	return t->prefix.str[0] == 0 ? NULL : t->prefix.str;
}

bool cmdline_insert(T *t, const char *key)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	ensure_space(t->left, 1);
	mbtowc(t->left.str+t->left.len, key, strlen(key));
	t->left.str[t->left.len + 1] = 0;
	t->left.len++;
	return 1;
}

bool cmdline_delete(T *t)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	if (t->left.len > 0) {
		t->left.str[t->left.len - 1] = 0;
		t->left.len--;
	}
	return 1;
}

bool cmdline_delete_right(T *t)
{
	int i;
	if (t->prefix.len == 0) {
		return 0;
	}
	if (t->right.len > 0) {
		for (i = 0; i < t->right.len; i++) {
			t->right.str[i] = t->right.str[i+1];
		}
		t->right.len--;
	}
	return 1;
}

bool cmdline_delete_word(cmdline_t *t)
{
	int i;
	if (t->prefix.len == 0 || t->left.len == 0) {
		return 0;
	}
	i = t->left.len - 1;
	while (i > 0 && !iswalnum(t->left.str[i]))
		i--;
	while (i > 0 && !iswpunct(t->left.str[i-1]) && !iswspace(t->left.str[i-1]))
		i--;
	t->left.len = i;
	t->left.str[i] = 0;
	return 1;
}

/* pass a ct argument to move over words? */
bool cmdline_left(T *t)
{
	int i;
	if (t->prefix.len == 0) {
		return 0;
	}
	if (t->left.len > 0) {
		ensure_space(t->right, 1);
		for (i = t->right.len; i >= 0; i--) {
			t->right.str[i + 1] = t->right.str[i];
		}
		t->right.len++;
		t->right.str[0] = t->left.str[t->left.len-1];
		t->left.str[t->left.len-1] = 0;
		t->left.len--;
	}
	return 1;
}

bool cmdline_right(T *t)
{
	int i;
	if (t->prefix.str[0] == 0) {
		return 0;
	}
	if (t->right.len > 0) {
		ensure_space(t->left, 1);
		t->left.str[t->left.len] = t->right.str[0];
		t->left.str[t->left.len+1] = 0;
		for (i = 0; i < t->right.len; i++) {
			t->right.str[i] = t->right.str[i + 1];
		}
		t->left.len++;
		t->right.len--;
	}
	return 1;
}

bool cmdline_home(T *t)
{
	int i;
	if (t->prefix.len == 0) {
		return 0;
	}
	if (t->left.len > 0) {
		ensure_space(t->right, (size_t) t->left.len + t->right.len);
		t->right.str[t->right.len + t->left.len] = 0;
		for (i = t->right.len; i >= 0; i--) {
			t->right.str[i + t->left.len] = t->right.str[i];
		}
		wcscpy(t->right.str, t->left.str);
		t->right.len += t->left.len;
		t->left.str[0] = 0;
		t->left.len = 0;
	}
	return 1;
}

bool cmdline_end(T *t)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	if (t->right.len > 0) {
		ensure_space(t->left, (size_t) t->right.len);
		wcscpy(t->left.str + t->left.len, t->right.str);
		/* t->left.str[t->left.len + t->right.len] = 0; */
		t->left.len += t->right.len;
		t->right.str[0] = 0;
		t->right.len = 0;
	}
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

bool cmdline_set(T *t, const char *line)
{
	if (t->prefix.len == 0) {
		return 0;
	}
	t->right.str[0] = 0;
	t->right.len = 0;
	ensure_space(t->left, strlen(line));
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
		ensure_space(t->buf, (size_t) t->left.len + t->right.len);
		size_t n = wcstombs(t->buf.str, t->left.str, t->left.len);
		if (n == (size_t) -1) {
			return "";
		}
		size_t m = wcstombs(t->buf.str + n, t->right.str, t->right.len);
		if (m == (size_t) -1) {
			return "";
		}
		t->buf.str[n+m] = 0;
	}
	return t->buf.str;
}

int cmdline_print(cmdline_t *t, struct ncplane *n)
{
	int ret = 0;
	ret += ncplane_putstr_yx(n, 0, 0, t->prefix.str);
	ret += ncplane_putwstr(n, t->left.str);
	ncplane_putwstr(n, t->right.str);
	return ret;
}

void cmdline_deinit(cmdline_t *t) {
	free(t->prefix.str);
	free(t->left.str);
	free(t->right.str);
	free(t->buf.str);
}

#undef vstr_init
#undef ensure_space
#undef ensure_capacity
#undef T
