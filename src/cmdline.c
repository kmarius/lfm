#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "cmdline.h"

#define T Cmdline

#define VSTR_INIT(vec, c) \
	do { \
		(vec).str = malloc(sizeof(*(vec).str) * ((c) + 1)); \
		(vec).cap = c; \
		(vec).str[0] = 0; \
		(vec).len = 0; \
	} while (0)


#define ENSURE_CAPACITY(vec, _sz) \
	do { \
		const size_t v_sz = _sz; \
		if ((vec).cap < v_sz) { \
			while ((vec).cap < v_sz) \
				(vec).cap *= 2; \
			(vec).str = realloc((vec).str, sizeof(*vec.str) * (vec).cap * 2 + 1); \
		} \
	} while (0)


#define ENSURE_SPACE(vec, sz) \
	ENSURE_CAPACITY(vec, (size_t) (vec).len + sz)


#define SHIFT_RIGHT(t, _sz) \
	do { \
		const size_t sz = _sz; \
		ENSURE_SPACE((t)->right, sz); \
		memmove((t)->right.str + sz, (t)->right.str, sizeof(wchar_t)*((t)->right.len+1)); \
		memcpy((t)->right.str, (t)->left.str + (t)->left.len - sz, sizeof(wchar_t)*sz); \
		(t)->right.len += sz; \
		(t)->left.len -= sz; \
		(t)->left.str[(t)->left.len] = 0; \
	} while (0)


#define SHIFT_LEFT(t, _sz) \
	do { \
		const size_t sz = _sz; \
		ENSURE_SPACE((t)->left, sz); \
		memcpy((t)->left.str + (t)->left.len, (t)->right.str, sizeof(wchar_t)*sz); \
		memmove((t)->right.str, (t)->right.str + sz, sizeof(wchar_t)*((t)->right.len-sz+1)); \
		(t)->right.len -= sz; \
		(t)->left.len += sz; \
		(t)->left.str[(t)->left.len] = 0; \
	} while (0)


void cmdline_init(T *t)
{
	VSTR_INIT(t->prefix, 8);
	VSTR_INIT(t->left, 8);
	VSTR_INIT(t->right, 8);
	VSTR_INIT(t->buf, 8);
}


void cmdline_deinit(T *t) {
	if (!t)
		return;

	free(t->prefix.str);
	free(t->left.str);
	free(t->right.str);
	free(t->buf.str);
}


bool cmdline_prefix_set(T *t, const char *prefix)
{
	if (!prefix)
		return false;

	const unsigned long l = strlen(prefix);
	ENSURE_CAPACITY(t->prefix, l);
	strcpy(t->prefix.str, prefix);
	t->prefix.len = l;
	return true;
}


const char *cmdline_prefix_get(T *t)
{
	return t->prefix.len == 0 ? NULL : t->prefix.str;
}


bool cmdline_insert(T *t, const char *key)
{
	if (t->prefix.len == 0) {
		return false;
	}
	ENSURE_SPACE(t->left, 1);
	mbtowc(t->left.str+t->left.len, key, strlen(key));
	t->left.len++;
	t->left.str[t->left.len] = 0;
	return true;
}


bool cmdline_delete(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	t->left.str[t->left.len - 1] = 0;
	t->left.len--;
	return true;
}


bool cmdline_delete_right(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0)
		return false;

	memmove(t->right.str, t->right.str+1, sizeof(wchar_t)*t->right.len);
	t->right.len--;
	return true;
}


bool cmdline_delete_word(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	int16_t i = t->left.len - 1;
	while (i > 0 && iswspace(t->left.str[i]))
		i--;
	while (i > 0 && t->left.str[i] == '/' && !iswspace(t->left.str[i-1]))
		i--;
	while (i > 0 && !(iswspace(t->left.str[i-1]) || t->left.str[i-1] == '/'))
		i--;
	t->left.len = i;
	t->left.str[i] = 0;
	return true;
}


bool cmdline_delete_line_left(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	t->left.len = 0;
	t->left.str[t->left.len] = 0;
	return true;
}


/* pass a ct argument to move over words? */
bool cmdline_left(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	SHIFT_RIGHT(t, 1);
	return true;
}


bool cmdline_word_left(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	int16_t i = t->left.len;
	if (i > 0 && iswpunct(t->left.str[i-1]))
		i--;
	if (i > 0 && iswspace(t->left.str[i-1]))
		while (i > 0 && iswspace(t->left.str[i-1]))
			i--;
	else
		while (i > 0 && !(iswspace(t->left.str[i-1]) || iswpunct(t->left.str[i-1])))
			i--;
	SHIFT_RIGHT(t, t->left.len-i);
	return true;
}


bool cmdline_word_right(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0)
		return false;

	int16_t i = 0;
	if (i < t->right.len && iswpunct(t->right.str[i]))
		i++;
	if (i < t->right.len && iswspace(t->right.str[i]))
		while (i < t->right.len && iswspace(t->right.str[i]))
			i++;
	else
		while (i < t->right.len && !(iswspace(t->right.str[i]) || iswpunct(t->right.str[i])))
			i++;
	SHIFT_LEFT(t, i);
	return true;
}


bool cmdline_right(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0)
		return false;

	SHIFT_LEFT(t, 1);
	return true;
}


bool cmdline_home(T *t)
{
	if (t->prefix.len == 0 || t->left.len == 0)
		return false;

	SHIFT_RIGHT(t, t->left.len);
	return true;
}


bool cmdline_end(T *t)
{
	if (t->prefix.len == 0 || t->right.len == 0)
		return false;

	SHIFT_LEFT(t, t->right.len);
	return true;
}


bool cmdline_clear(T *t)
{
	t->prefix.str[0] = 0;
	t->prefix.len = 0;
	t->left.str[0] = 0;
	t->left.len = 0;
	t->right.str[0] = 0;
	t->right.len = 0;
	return true;
}


bool cmdline_set_whole(T *t, const char *prefix, const char *left, const char *right)
{
	ENSURE_SPACE(t->left, strlen(left));
	ENSURE_SPACE(t->right, strlen(right));
	cmdline_prefix_set(t, prefix);
	size_t n = mbstowcs(t->left.str, left, t->left.cap + 1);
	if (n == (size_t) -1) {
		t->left.len = 0;
		t->left.str[0] = 0;
	} else {
		t->left.len = n;
	}
	n = mbstowcs(t->right.str, right, t->right.cap + 1);
	if (n == (size_t) -1) {
		t->right.len = 0;
		t->right.str[0] = 0;
	} else {
		t->right.len = n;
	}
	return true;
}


bool cmdline_set(T *t, const char *line)
{
	if (t->prefix.len == 0)
		return false;

	t->right.str[0] = 0;
	t->right.len = 0;
	ENSURE_SPACE(t->left, strlen(line));
	const size_t n = mbstowcs(t->left.str, line, t->left.cap + 1);
	if (n == (size_t) -1) {
		t->left.len = 0;
		t->left.str[0] = 0;
	} else {
		t->left.len = n;
	}
	return true;
}


const char *cmdline_get(T *t)
{
	t->buf.str[0] = 0;
	if (t->prefix.len != 0) {
		ENSURE_SPACE(t->buf, (size_t) (t->left.len + t->right.len) * MB_CUR_MAX);
		size_t n = wcstombs(t->buf.str, t->left.str, t->left.len * MB_CUR_MAX);
		if (n == (size_t) -1)
			return "";

		size_t m = wcstombs(t->buf.str + n, t->right.str, t->right.len * MB_CUR_MAX);
		if (m == (size_t) -1)
			return "";

		t->buf.str[n+m] = 0;
	}
	return t->buf.str;
}


uint16_t cmdline_print(T *t, struct ncplane *n)
{
	int ncol, offset;
	ncplane_dim_yx(n, NULL, &ncol);

	uint16_t ret = 0;
	ret += ncplane_putstr_yx(n, 0, 0, t->prefix.str);
	ncol -= ret;

	if (t->right.len == 0)
		offset = t->left.len - ncol + 1;
	else if (t->right.len > ncol / 2)
		offset = t->left.len - ncol + ncol / 2 + 1;
	else
		offset = t->left.len - ncol + t->right.len + 1;
	ret += ncplane_putwstr(n, t->left.str + (offset > 0 ? offset : 0));
	ncplane_putwstr(n, t->right.str);

	printf(t->right.len == 0 ? "\033[2 q" : "\033[6 q");  // block/bar cursor

	return ret;
}
