#include <notcurses/notcurses.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cmdline.h"
#include "util.h"

#define T cmdline_t

void cmdline_init(cmdline_t *t)
{
	t->prefix[0] = 0;
	t->left[0] = 0;
	t->right[0] = 0;
}

int cmdline_prefix_set(T *t, const char *prefix)
{
	if (!prefix) {
		return 0;
	}
	mbstowcs(t->prefix, prefix, sizeof(t->prefix) - 1);
	return 1;
}

const wchar_t *cmdline_prefix_get(T *t)
{
	return t->prefix[0] == 0 ? NULL : t->prefix;
}

int cmdline_insert(T *t, const char *key)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	const int l = wcslen(t->left);
	if (l >= ACC_SIZE - 1) {
		return 0;
	}
	mbtowc(t->left+l, key, strlen(key));
	t->left[l + 1] = 0;
	return 1;
}

int cmdline_delete(T *t)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	const int l = wcslen(t->left);
	if (l > 0) {
		t->left[l - 1] = 0;
	}
	return 1;
}

int cmdline_delete_right(T *t)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	wchar_t *c = t->right;
	if (*c) {
		while (*++c)
			*(c-1) = *c;
		*(c-1) = '\0';
	}
	return 1;
}

/* pass a ct argument to move over words? */
int cmdline_left(T *t)
{
	int j;
	if (t->prefix[0] == 0) {
		return 0;
	}
	const int l = wcslen(t->left);
	if (l > 0) {
		j = min(wcslen(t->right), ACC_SIZE - 2);
		t->right[j + 1] = 0;
		for (; j > 0; j--) {
			t->right[j] = t->right[j - 1];
		}
		t->right[0] = t->left[l - 1];
		t->left[l - 1] = 0;
	}
	return 1;
}

int cmdline_right(T *t)
{
	int i;
	if (t->prefix[0] == 0) {
		return 0;
	}
	const int l = wcslen(t->left);
	const int j = wcslen(t->right);
	if (j > 0) {
		if (l < ACC_SIZE - 2) {
			t->left[l] = t->right[0];
			t->left[l + 1] = 0;
			for (i = 0; i < j; i++) {
				t->right[i] = t->right[i + 1];
			}
		}
	}
	return 1;
}

int cmdline_home(T *t)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	const int l = wcslen(t->left);
	if (l > 0) {
		int j = min(wcslen(t->right) - 1 + l, ACC_SIZE - 1 - l);
		t->right[j + 1] = 0;
		for (; j >= l; j--) {
			t->right[j] = t->right[j - l];
		}
		wcsncpy(t->right, t->left, l);
		t->left[0] = 0;
	}
	return 1;
}

int cmdline_end(T *t)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	if (t->right[0] != 0) {
		const int j = wcslen(t->left);
		const int l = ACC_SIZE - 1 - wcslen(t->left);
		wcsncpy(t->left + j, t->right, l);
		t->left[l + j] = 0;
		t->right[0] = 0;
	}
	return 1;
}

int cmdline_clear(T *t)
{
	t->prefix[0] = 0;
	t->left[0] = 0;
	t->right[0] = 0;
	return 1;
}

int cmdline_set(T *t, const char *line)
{
	if (t->prefix[0] == 0) {
		return 0;
	}
	mbstowcs(t->left, line, sizeof(t->left) - 1);
	t->right[0] = 0;
	return 1;
}

const char *cmdline_get(const T *t)
{
	static char buf[2 * ACC_SIZE * 4] = {0};
	if (t->prefix[0] == 0) {
		return "";
	} else {
		size_t n = wcstombs(buf, t->left, sizeof(t->left) - 1);
		if (n == (size_t) - 1) {
			// could not convert
			return buf;
		}
		wcstombs(buf + n, t->right, sizeof(t->right) - n - 1);
	}
	return buf;
}

int cmdline_print(cmdline_t *t, struct ncplane *n)
{
	int ret = 0;
	ret += ncplane_putwstr_yx(n, 0, 0, t->prefix);
	ret += ncplane_putwstr(n, t->left);
	ncplane_putwstr(n, t->right);
	return ret;
}

void cmdline_deinit(cmdline_t *t) {
	(void) t;
}

#undef T
