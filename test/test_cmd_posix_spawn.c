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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#if __linux__
#include <features.h>
#endif
#include <spawn.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__GLIBC_PREREQ)
#define __GLIBC_PREREQ(a, b) 0
#endif

/* Performs a posix_spawnp() / waitpid() pair.
 * The command to execute and its parameters are taken from the command line, one by one.
 * Returns 0 (unless an error occurred), not the command's exit code. */

#ifdef __APPLE__
extern char **environ;
#endif

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "need at least 1 argument\n");
    return 1;
  }

  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
#ifdef __APPLE__
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
  /* Test with all kinds of file_actions */
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  posix_spawn_file_actions_addopen(&file_actions, 96, "integration.bats", O_RDWR, 0);
  posix_spawn_file_actions_addopen(&file_actions, 97, ".", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&file_actions, 97, 98);
  posix_spawn_file_actions_adddup2(&file_actions, 1, 1);
  posix_spawn_file_actions_addclose(&file_actions, 97);
#if __GLIBC_PREREQ(2, 29) || defined(__APPLE__)
  posix_spawn_file_actions_addchdir_np(&file_actions, ".");
  /* This call fails on macOS when using an fd opened by the posix spawn file actions. */
  posix_spawn_file_actions_addfchdir_np(&file_actions,
#ifdef __APPLE__
                                        open(".", O_RDONLY, 0));
#else
                                        98);
#endif
#endif
#if __GLIBC_PREREQ(2, 34)
  posix_spawn_file_actions_addclosefrom_np(&file_actions, 94);
#endif
#ifdef __APPLE__
  posix_spawn_file_actions_addinherit_np(&file_actions, 0);
  posix_spawn_file_actions_addinherit_np(&file_actions, 1);
  posix_spawn_file_actions_addinherit_np(&file_actions, 2);
#endif
  pid_t pid;
  int ret = posix_spawnp(&pid, argv[1], &file_actions, &attributes, argv + 1, environ);
  if (ret) {
    errno = ret;
    perror("posix_spawnp");
    return 1;
  }
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&file_actions);
  if (waitpid(pid, NULL, 0) < 0) {
    perror("waitpid");
    return 1;
  }

  return 0;
}
