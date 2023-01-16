/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Based on clone(2) man page. */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)    /* Stack size for cloned child */

/* Performs a clone() / waitpid() pair.
 * The command executes and its parameter taken from the command line.
 * Returns 0 (unless an error occurred), not the command's exit code. */

int child(void *arg) {
  return execv(*(char**)arg, (char**)arg);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "need at least 1 argument\n");
    return 1;
  }

  char *stack, *stack_top;
  stack = malloc(STACK_SIZE);
  if (!stack) {
    perror("malloc");
    return 1;
  }
  stack_top = stack + STACK_SIZE;
  /* This clone can be intercepted. */
  int ret = clone(child, stack_top, CLONE_VFORK|SIGCHLD, &argv[1]);
  if (ret == -1) {
    errno = ret;
    perror("clone");
    return 1;
  }
  /* This one disables interception. */
  ret = clone(child, stack_top, CLONE_PTRACE|SIGCHLD, &argv[1]);
  if (ret == -1) {
    errno = ret;
    perror("clone");
    return 1;
  }

  if (waitpid(ret, NULL, 0) < 0) {
    perror("waitpid");
    return 1;
  }

  return 0;
}
