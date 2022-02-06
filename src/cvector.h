// The MIT License (MIT)
//
// Copyright (c) 2015 Evan Teran
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/* Based on https://github.com/eteran/c-vector/blob/master/cvector.h
 * Changes: always use logarithmic grow
 *          startsize is 4
 *          add macro cvector_ffree to pass a function/macro to free elements
 *          ...
 */

#pragma once

#include <assert.h> /* for assert */
#include <stdlib.h> /* for malloc/realloc/free */
#include <string.h> /* for memcpy */

#define CVECTOR_INITIAL_CAPACITY 8

/**
 * @brief cvector_vector_type - The vector type used in this library
 */
#define cvector_vector_type(type) type *

/**
 * @brief cvector_set_capacity - For internal use, sets the capacity variable of
 * the vector
 * @param vec - the vector
 * @param size - the new capacity to set
 * @return void
 */
#define cvector_set_capacity(vec, size) \
	do { \
		if (vec) { \
			((size_t *)(vec))[-1] = (size); \
		} \
	} while (0)

/**
 * @brief cvector_set_size - For internal use, sets the size variable of the
 * vector
 * @param vec - the vector
 * @param size - the new capacity to set
 * @return void
 */
#define cvector_set_size(vec, size) \
	do { \
		if (vec) { \
			((size_t *)(vec))[-2] = (size); \
		} \
	} while (0)

/**
 * @brief cvector_capacity - gets the current capacity of the vector
 * @param vec - the vector
 * @return the capacity as a size_t
 */
#define cvector_capacity(vec) ((vec) ? ((size_t *)(vec))[-1] : (size_t)0)

/**
 * @brief cvector_size - gets the current size of the vector
 * @param vec - the vector
 * @return the size as a size_t
 */
#define cvector_size(vec) ((vec) ? ((size_t *)(vec))[-2] : (size_t)0)

/**
 * @brief cvector_empty - returns non-zero if the vector is empty
 * @param vec - the vector
 * @return non-zero if empty, zero if non-empty
 */
#define cvector_empty(vec) (cvector_size(vec) == 0)

/**
 * @brief cvector_grow - For internal use, ensures that the vector is at least
 * <count> elements big
 * @param vec - the vector
 * @param count - the new capacity to set
 * @return void
 */
#define cvector_grow(vec, count) \
	do { \
		const size_t cv_sz = \
		(count) * sizeof(*(vec)) + (sizeof(size_t) * 2); \
		if (!(vec)) { \
			size_t *cv_p = malloc(cv_sz); \
			assert(cv_p); \
			(vec) = (void *)(&cv_p[2]); \
			cvector_set_capacity((vec), (count)); \
			cvector_set_size((vec), 0); \
		} else { \
			size_t *cv_p1 = &((size_t *)(vec))[-2]; \
			size_t *cv_p2 = realloc(cv_p1, (cv_sz)); \
			assert(cv_p2); \
			(vec) = (void *)(&cv_p2[2]); \
			cvector_set_capacity((vec), (count)); \
		} \
	} while (0)

/**
 * @brief cvector_ensure_capacity - For internal use, ensures that the vector is at least
 * <count> elements big
 * @param vec - the vector
 * @param count - the new capacity to set
 * @return void
 */
#define cvector_ensure_capacity(vec, count) \
	do { \
		size_t cv_cap = cvector_capacity(vec); \
		if (cv_cap < count) { \
			if (cv_cap == 0) { \
				cv_cap = CVECTOR_INITIAL_CAPACITY; \
			} \
			while (cv_cap < count) { \
				cv_cap *= 2; \
			} \
			cvector_grow((vec), cv_cap); \
		} \
	} while (0)

/**
 * @brief cvector_ensure_space - For internal use, ensures that the vector has at least
 * space for <count> more elements
 * @param vec - the vector
 * @param count - the new capacity to set
 * @return void
 */
#define cvector_ensure_space(vec, count) \
	do { \
		cvector_ensure_capacity((vec), cvector_size(vec) + count); \
	} while (0)

/**
 * @brief cvector_pop_back - removes the last element from the vector
 * @param vec - the vector
 * @return void
 */
#define cvector_pop_back(vec) \
	do { \
		cvector_set_size((vec), cvector_size(vec) - 1); \
	} while (0)

/**
 * @brief cvector_erase - removes the element at index i from the vector
 * @param vec - the vector
 * @param i - index of element to remove
 * @return void
 */
#define cvector_erase(vec, i) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			if ((i) < cv_sz) { \
				cvector_set_size((vec), cv_sz - 1); \
				size_t cv_x; \
				for (cv_x = (i); cv_x < (cv_sz - 1); ++cv_x) { \
					(vec)[cv_x] = (vec)[cv_x + 1]; \
				} \
			} \
		} \
	} while (0)

/**
 * @brief cvector_swap_erase - removes the element at index i from the vector
 * and replaces it with the last element
 * @param vec - the vector
 * @param i - index of element to remove
 * @return void
 */
#define cvector_swap_erase(vec, i) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			if ((i) < cv_sz) { \
				(vec)[(i)] = (vec)[cv_sz - 1]; \
				cvector_set_size((vec), cv_sz - 1); \
			} \
		} \
	} while (0)

/**
 * @brief cvector_swap_ferase - removes the element at index i from the vector
 * and replaces it with the last element, with an additional parameter to free
 * the object
 * @param vec - the vector
 * @param i - index of element to remove
 * @return void
 */
#define cvector_swap_ferase(vec, free, i) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			if ((i) < cv_sz) { \
				free((vec)[i]); \
				(vec)[(i)] = (vec)[cv_sz - 1]; \
				cvector_set_size((vec), cv_sz - 1); \
			} \
		} \
	} while (0)

/**
 * @brief cvector_erase - removes the element at index i from the vector with an
 * additional parameter to free the object
 * @param vec - the vector
 * @param i - index of element to remove
 * @return void
 */
#define cvector_ferase(vec, free, i) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			if ((i) < cv_sz) { \
				free((vec)[i]); \
				cvector_set_size((vec), cv_sz - 1); \
				size_t cv_x; \
				for (cv_x = (i); cv_x < (cv_sz - 1); ++cv_x) { \
					(vec)[cv_x] = (vec)[cv_x + 1]; \
				} \
			} \
		} \
	} while (0)

/**
 * @brief cvector_swap_remove - removes the element from the vector and replaces it with the last. equality is checked via ==
 * @param vec - the vector
 * @param e - the element to remove
 * @return void
 */
#define cvector_swap_remove(vec, e) \
	do { \
		for (size_t cv_i = 0; cv_i < cvector_size((vec)); cv_i++) { \
			if ((vec)[cv_i] == e) { \
				cvector_swap_erase((vec), cv_i); \
				break; \
			} \
		} \
	} while (0)

/**
 * @brief cvector_free - frees all memory associated with the vector
 * @param vec - the vector
 * @return void
 */
#define cvector_free(vec) \
	do { \
		if (vec) { \
			size_t *p1 = &((size_t *)(vec))[-2]; \
			free(p1); \
		} \
	} while (0)

/**
 * @brief cvector_ffree - calls a function to free each element and frees the
 * vector
 * @param vec - the vector
 * @param free - a function/macro used to free each object
 * @return void
 */
#define cvector_ffree(vec, free) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			for (size_t cv_i = 0; cv_i < cv_sz; ++cv_i) { \
				free((vec)[cv_i]); \
			} \
			cvector_free(vec); \
		} \
	} while (0)

/**
 * @brief cvector_fclear - calls a function to free each element and sets the size to zero
 * @param vec - the vector
 * @param free - a function/macro used to free each object
 * @return void
 */
#define cvector_fclear(vec, free) \
	do { \
		if (vec) { \
			const size_t cv_sz = cvector_size(vec); \
			for (size_t cv_i = 0; cv_i < cv_sz; ++cv_i) { \
				free((vec)[cv_i]); \
			} \
			cvector_set_size(vec, 0); \
		} \
	} while (0)

/**
 * @brief cvector_begin - returns an iterator to first element of the vector
 * @param vec - the vector
 * @return a pointer to the first element (or NULL)
 */
#define cvector_begin(vec) (vec)

/**
 * @brief cvector_end - returns an iterator to one past the last element of the
 * vector
 * @param vec - the vector
 * @return a pointer to one past the last element (or NULL)
 */
#define cvector_end(vec) ((vec) ? &((vec)[cvector_size(vec)]) : NULL)

/**
 * @brief cvector_push_back - adds an element to the end of the vector
 * @param vec - the vector
 * @param value - the value to add
 * @return void
 */
#define cvector_push_back(vec, value) \
	do { \
		cvector_ensure_space(vec, 1); \
		(vec)[cvector_size(vec)] = (value); \
		cvector_set_size((vec), cvector_size(vec) + 1); \
	} while (0)

/**
 * @brief cvector_copy - copy a vector
 * @param from - the original vector
 * @param to - destination to which the function copy to
 * @return void
 */
#define cvector_copy(from, to) \
	do { \
		cvector_grow(to, cvector_size(from)); \
		cvector_set_size(to, cvector_size(from)); \
		memcpy(to, from, cvector_size(from) * sizeof(*(from))); \
	} while (0)

/**
 * @brief cvector_compact Remove NULL elements from a vector. Retains order.
 * @param vec - the vector
 * @return void
 */
#define cvector_compact(vec) \
	do { \
		size_t j = 0; \
		size_t sz = cvector_size((vec)); \
		for (size_t cv_i = 0; cv_i < sz; cv_i++) { \
			if (vec[cv_i]) \
				vec[j++] = vec[cv_i]; \
		} \
		cvector_set_size(vec, j); \
	} while (0)

/* statement exprs are a GNU extension */
/**
 * @brief cvector_contains_str Check if a vector contains a string.
 * @param vec - the vector
 * @param str - the string
 * @return void
 */
#define cvector_contains_str(vec, str) ({ \
		size_t sz = cvector_size((vec)); \
		bool ret = false; \
		for (size_t cv_i = 0; cv_i < sz; cv_i++) { \
			if ((vec)[cv_i] && *(vec)[cv_i] == *(str) && streq((vec)[cv_i], (str))) { \
				ret = true; \
				break; \
			} \
		} \
		ret; })
