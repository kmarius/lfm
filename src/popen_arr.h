// Implemented by Vitaly _Vi Shukela in 2013, License=MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <stdio.h>

/**
 * Fork and exec the program, enabling stdio access to stdin and stdout of the
 * program You may close opened streams with fclose. Note: the procedure does no
 * signal handling except of signal(SIGPIPE, SIG_IGN); You should waitpid for
 * the returned PID to collect the zombie or use signal(SIGCHLD, SIG_IGN);
 *
 * @arg in  stdin of the program, to be written to. If NULL then not redirected
 * @arg out stdout of the program, to be read from. If NULL then not redirected
 * @arg err stderr of the program, to be read from. If NULL then not redirected
 * @arg program full path of the program, without reference to $PATH
 * @arg argv NULL terminated array of strings, program arguments (includiong
 * program name)
 * @arg envp NULL terminated array of environment variables, NULL => preserve
 * environment
 * @return PID of the program or -1 if failed
 */
int popen2_arr(FILE **in, FILE **out, FILE **err, const char *program,
               char *const argv[], const char *pwd);

/** like popen2_arr, but uses execvp/execvpe instead of execve/execv, so looks
 * up $PATH */
int popen2_arr_p(FILE **in, FILE **out, FILE **err, const char *program,
                 char *const argv[], const char *pwd);

/**
 * Simplified interface to popen2_arr.
 * You may close the returned stream with fclose.
 * Note: the procedure does no signal handling except of signal(SIGPIPE,
 * SIG_IGN); You should wait(2) after closing the descriptor to collect zombie
 * process or use signal(SIGCHLD, SIG_IGN)
 *
 * @arg program program name, can rely on $PATH
 * @arg argv program arguments, NULL-terminated const char* array
 * @arg pipe_into_program 1 to be like popen(...,"w"), 0 to be like
 * popen(...,"r")
 * @return FILE* instance or NULL if error
 */
FILE *popen_arr(const char *program, char *const argv[], int pipe_into_program);
