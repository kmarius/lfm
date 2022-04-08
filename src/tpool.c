// Copyright John Schember <john@nachtimwald.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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
//
// See https://nachtimwald.com/2019/04/12/thread-pool-in-c/

#include <pthread.h>
#include <stdlib.h>

#include "tpool.h"

struct tpool_work {
	thread_func_t func;
	void *arg;
	struct tpool_work *next;
};
typedef struct tpool_work tpool_work_t;

struct tpool {
	tpool_work_t *work_first;
	tpool_work_t *work_last;
	pthread_mutex_t work_mutex;
	pthread_cond_t work_cond;
	pthread_cond_t working_cond;
	size_t working_cnt;
	size_t thread_cnt;
	bool stop;
};

static tpool_work_t *tpool_work_create(thread_func_t func, void *arg)
{
	tpool_work_t *work;

	if (func == NULL)
		return NULL;

	work = malloc(sizeof(*work));
	work->func = func;
	work->arg = arg;
	work->next = NULL;
	return work;
}

static void tpool_work_destroy(tpool_work_t *work)
{
	if (work == NULL)
		return;
	free(work);
}

static tpool_work_t *tpool_work_get(tpool_t *tm)
{
	tpool_work_t *work;

	if (tm == NULL)
		return NULL;

	work = tm->work_first;
	if (work == NULL)
		return NULL;

	if (work->next == NULL) {
		tm->work_first = NULL;
		tm->work_last = NULL;
	} else {
		tm->work_first = work->next;
	}

	return work;
}

static void *tpool_worker(void *arg)
{
	tpool_t *tm = arg;
	tpool_work_t *work;

	while (1) {
		pthread_mutex_lock(&(tm->work_mutex));

		while (tm->work_first == NULL && !tm->stop)
			pthread_cond_wait(&(tm->work_cond), &(tm->work_mutex));

		if (tm->stop)
			break;

		work = tpool_work_get(tm);
		tm->working_cnt++;
		pthread_mutex_unlock(&(tm->work_mutex));

		if (work != NULL) {
			work->func(work->arg);
			tpool_work_destroy(work);
		}

		pthread_mutex_lock(&(tm->work_mutex));
		tm->working_cnt--;
		if (!tm->stop && tm->working_cnt == 0 && tm->work_first == NULL)
			pthread_cond_signal(&(tm->working_cond));
		pthread_mutex_unlock(&(tm->work_mutex));
	}

	tm->thread_cnt--;
	pthread_cond_signal(&(tm->working_cond));
	pthread_mutex_unlock(&(tm->work_mutex));
	return NULL;
}

tpool_t *tpool_create(size_t num)
{
	tpool_t *tm;
	pthread_t thread;
	size_t i;

	if (num == 0)
		num = 2;

	tm = calloc(1, sizeof(*tm));
	tm->thread_cnt = num;

	pthread_mutex_init(&(tm->work_mutex), NULL);
	pthread_cond_init(&(tm->work_cond), NULL);
	pthread_cond_init(&(tm->working_cond), NULL);

	tm->work_first = NULL;
	tm->work_last = NULL;

	for (i = 0; i < num; i++) {
		pthread_create(&thread, NULL, tpool_worker, tm);
		pthread_detach(thread);
	}

	return tm;
}

void tpool_destroy(tpool_t *tm)
{
	tpool_work_t *work;
	tpool_work_t *work2;

	if (tm == NULL)
		return;

	pthread_mutex_lock(&(tm->work_mutex));
	work = tm->work_first;
	while (work != NULL) {
		work2 = work->next;
		tpool_work_destroy(work);
		work = work2;
	}
	tm->stop = true;
	pthread_cond_broadcast(&(tm->work_cond));
	pthread_mutex_unlock(&(tm->work_mutex));

	tpool_wait(tm);

	pthread_mutex_destroy(&(tm->work_mutex));
	pthread_cond_destroy(&(tm->work_cond));
	pthread_cond_destroy(&(tm->working_cond));

	free(tm);
}

bool tpool_add_work(tpool_t *tm, thread_func_t func, void *arg)
{
	tpool_work_t *work;

	if (tm == NULL)
		return false;

	work = tpool_work_create(func, arg);
	if (work == NULL)
		return false;

	// adding work at the front of the queue (as of 2022-04-08)
	pthread_mutex_lock(&(tm->work_mutex));
	if (tm->work_first == NULL) {
		tm->work_first = work;
		tm->work_last = tm->work_first;
	} else {
		work->next = tm->work_first;
		tm->work_first = work;
	}

	pthread_cond_broadcast(&(tm->work_cond));
	pthread_mutex_unlock(&(tm->work_mutex));

	return true;
}

void tpool_wait(tpool_t *tm)
{
	if (tm == NULL)
		return;

	pthread_mutex_lock(&(tm->work_mutex));
	while (1) {
		if ((!tm->stop && tm->working_cnt != 0) ||
				(tm->stop && tm->thread_cnt != 0)) {
			pthread_cond_wait(&(tm->working_cond),
					&(tm->work_mutex));
		} else {
			break;
		}
	}
	pthread_mutex_unlock(&(tm->work_mutex));
}
