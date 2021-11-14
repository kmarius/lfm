//   Copyright John Schember <john@nachtimwald.com>
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

// Implemented by Vitaly _Vi Shukela in 2013, License=MIT

/* Changes: */
/* capture stderr */

#define _GNU_SOURCE // execvpe
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "popen_arr.h"

static int popen2_impl(FILE** in, FILE** out, FILE** err, const char* program,
		const char* const argv[], const char* const envp[], int lookup_path)
{
	int child_stdout = -1;
	int child_stdin = -1;
	int child_stderr = -1;

	int to_be_written = -1;
	int to_be_read = -1;
	int to_be_read2 = -1;

	if (in) {
		int p[2] = {-1, -1};
		int ret = pipe(p);
		if (ret != 0) {
			return -1;
		}
		to_be_written = p[1];
		child_stdin = p[0];
		*in = fdopen(to_be_written, "w");
		if (*in == NULL) {
			close(to_be_written);
			close(child_stdin);
			return -1;
		}
	}
	if (out) {
		int p[2] = {-1, -1};
		int ret = pipe(p);
		if (ret != 0) {
			if (in) {
				close(child_stdin);
				fclose(*in);
				*in = NULL;
			}
			return -1;
		}
		to_be_read = p[0];
		child_stdout = p[1];
		*out = fdopen(to_be_read, "r");
	}
	if (err) {
		int p[2] = {-1, -1};
		int ret = pipe(p);
		if (ret != 0) {
			if (in) {
				close(child_stdin);
				fclose(*in);
				*in = NULL;
			}
			if (out) {
				close(child_stdout);
				fclose(*out);
				*out = NULL;
			}
			return -1;
		}
		to_be_read2 = p[0];
		child_stderr = p[1];
		*err = fdopen(to_be_read2, "r");
	}

	int childpid = fork();

	if (!childpid) {
		if (child_stdin != -1) {
			close(to_be_written);
			dup2(child_stdin, 0);
			close(child_stdin);
		}
		if (child_stdout != -1) {
			close(to_be_read);
			dup2(child_stdout, 1);
			close(child_stdout);
		}
		if (child_stderr != -1) {
			close(to_be_read2);
			dup2(child_stderr, 2);
			close(child_stderr);
		} else {
			close(2);
		}
		if (lookup_path) {
			if (envp) {
				execvpe(program, (char**) argv, (char**) envp);
			} else {
				execvp(program, (char**) argv);
			}
		} else {
			if (envp) {
				execve(program, (char**) argv, (char**) envp);
			} else {
				execv(program, (char**) argv);
			}
		}
		_exit(ENOSYS);
	}

	if (child_stdout != -1) {
		close(child_stdout);
	}
	if (child_stdin != -1) {
		close(child_stdin);
	}
	if (child_stderr != -1) {
		close(child_stderr);
	}

	return childpid;
}

int popen2_arr(FILE **in, FILE **out, FILE **err, const char *program,
		const char *const argv[], const char *const envp[])
{
	signal(SIGPIPE, SIG_IGN);
	return popen2_impl(in, out, err, program, argv, envp, 0);
}

int popen2_arr_p(FILE **in, FILE **out, FILE **err, const char *program,
		const char *const argv[], const char *const envp[])
{
	signal(SIGPIPE, SIG_IGN);
	return popen2_impl(in, out, err, program, argv, envp, 1);
}

FILE* popen_arr(const char* program, const char *const argv[], int pipe_into_program)
{
	FILE* f = NULL;
	if (pipe_into_program) {
		popen2_arr_p(&f, NULL, NULL, program, argv, NULL);
	} else {
		popen2_arr_p(NULL, &f, NULL, program, argv, NULL);
	}
	return f;
}
